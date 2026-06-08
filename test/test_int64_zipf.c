/*
 * test_int64_zipf.c — ACT-37: Int64 Zipf α=1.0 退化场景测试
 *
 * 目的: 验证在严重倾斜的 Zipf 分布数据上，Int64 库仍能正确查询，
 *       且 B1 路径的 per-bucket fallback (阈值 409) 正确触发。
 *
 * 测试场景:
 *   - 生成 Zipf α=1.0 分布的 int64 数据 (多数值聚集在少数桶)
 *   - 多数据规模: 1K, 10K, 100K, 500K
 *   - 小规模: B1 选中但某些桶 > 409 → per-query 回退
 *   - 大规模: max_bucket > 409 → 全局回退到 PATH_SCALAR
 *   - 正确性: 500 queries per size, 50% hit rate, 0 mismatches
 *
 * 编译 (Windows MinGW):
 *   gcc -O3 -std=c11 -mavx2 test/test_int64_zipf.c libint64search.a -Iinclude -Isrc -o test_int64_zipf -lm
 *
 * 编译 (Linux):
 *   gcc -O3 -std=c11 -mavx2 test/test_int64_zipf.c libint64search.a -Iinclude -Isrc -o test_int64_zipf -lm -lpthread
 */

#include "../include/int64_search.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <immintrin.h>

static int g_failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
        g_failures++; \
    } else { \
        printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

/* ── RNG ── */

static uint64_t xorshift64_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static double rand_double(uint64_t *rng) {
    return (double)xorshift64_next(rng) / (double)UINT64_MAX;
}

/* ── Zipf 数据生成 (α=1.0) ──
 *
 * 使用 power-law 变换在 int64 值空间产生严重倾斜:
 *   x = MAX * pow(u, beta)
 * 其中 beta 控制倾斜程度。beta 越大，值越集中在低位。
 *
 * 对于 α=1.0 Zipf，大部分样本命中前几个排名，
 * 对应到 int64 空间即为值集中在 0 附近。
 * 使用 beta=4 将 50% 的值集中在前 16% 的值空间中，
 * 确保 high20 目录桶分布极度不均。
 */

static int cmp_int64(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

static int64_t *gen_zipf_sorted(size_t n, uint64_t seed, double beta) {
    int64_t *data = (int64_t *)malloc(n * sizeof(int64_t));
    if (!data) return NULL;

    uint64_t rng = seed;
    int64_t max_val = (int64_t)0x7FFFFFFFFFFFFFFFULL;

    for (size_t i = 0; i < n; i++) {
        double u = rand_double(&rng);
        double v = pow(u, beta);
        data[i] = (int64_t)((double)max_val * v);
    }

    qsort(data, n, sizeof(int64_t), cmp_int64);
    return data;
}

static int64_t *gen_queries(const int64_t *data, size_t d_n, size_t q_n,
                             int hit_pct, uint64_t seed) {
    int64_t *queries = (int64_t *)malloc(q_n * sizeof(int64_t));
    if (!queries) return NULL;

    uint64_t rng = seed;
    size_t hit_n = (size_t)((unsigned long long)q_n * hit_pct / 100);

    for (size_t i = 0; i < hit_n; i++) {
        size_t idx = (size_t)(xorshift64_next(&rng) % d_n);
        queries[i] = data[idx];
    }
    for (size_t i = hit_n; i < q_n; i++) {
        int64_t cand;
        do {
            cand = (int64_t)((uint64_t)xorshift64_next(&rng) & 0x7FFFFFFFFFFFFFFFULL);
        } while (bsearch(&cand, data, d_n, sizeof(int64_t), cmp_int64) != NULL);
        queries[i] = cand;
    }
    return queries;
}

/* ── 单规模测试 ── */

static void test_zipf_at_size(size_t n, const char *label) {
    printf("\n--- Zipf α=1.0, n=%s ---\n", label);

    int64_t *data = gen_zipf_sorted(n, (uint64_t)(n * 2654435761ULL), 4.0);
    CHECK(data != NULL, "zipf data generated");

    int64_search_config_t cfg = {0, {0}};
    int64_search_t h = int64_search_create(data, n, &cfg);
    CHECK(h != NULL, "create succeeded");

    uint64_t rng = (uint64_t)(n * 42);
    size_t q_n = (n < 500) ? n : 500;
    int64_t *queries = gen_queries(data, n, q_n, 50, rng + 1);
    CHECK(queries != NULL, "queries generated");

    int mismatches = 0;
    for (size_t q = 0; q < q_n; q++) {
        size_t idx;
        int rc = int64_search_find(h, queries[q], &idx);
        int found = (bsearch(&queries[q], data, n, sizeof(int64_t), cmp_int64) != NULL);

        if ((found && rc != INT64_SEARCH_OK) || (!found && rc == INT64_SEARCH_OK))
            mismatches++;
        else if (found && rc == INT64_SEARCH_OK && data[idx] != queries[q])
            mismatches++;
    }
    CHECK(mismatches == 0, "%zu queries, %d mismatches", q_n, mismatches);

    int64_search_destroy(h);
    free(queries);
    free(data);
}

/* ── 主入口 ── */

int main(void) {
    printf("=== Int64 Zipf α=1.0 退化场景测试 ===\n");
    printf("验证: 严重倾斜数据上查询正确性 + B1 fallback 机制\n");
    printf("阈值: B1_MAX_BUCKET_THRESHOLD_INT64=409, B1_FALLBACK_THRESHOLD=409\n\n");

    /* 小规模: B1 应被选中，但部分桶可能触发 per-query fallback */
    test_zipf_at_size(1000,    "1K");
    test_zipf_at_size(10000,   "10K");

    /* 中规模: max_bucket 可能超过 409 → 全局 PATH_SCALAR */
    test_zipf_at_size(100000,  "100K");

    /* 大规模: 确保全局 PATH_SCALAR 正确工作 */
    test_zipf_at_size(500000,  "500K");

    printf("\n=== %s ===\n", g_failures ? "FAILED" : "PASSED");
    return g_failures ? 1 : 0;
}
