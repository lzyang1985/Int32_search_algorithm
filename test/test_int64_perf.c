/*
 * test_int64_perf.c — ACT-36: Int64 10M uniform 性能回归测试
 *
 * 编译 (Windows MinGW):
 *   gcc -O3 -std=c11 -mavx2 test/test_int64_perf.c libint64search.a -Iinclude -Isrc -o test_int64_perf -lm
 *
 * 编译 (Linux):
 *   gcc -O3 -std=c11 -mavx2 test/test_int64_perf.c libint64search.a -Iinclude -Isrc -o test_int64_perf -lm -lpthread
 *
 * 回归门禁:
 *   GATE-PERF: API cy/query ≤ 5000 @ 10M  (若超过则 FAIL)
 *   GATE-CORR: 全部查询结果正确 (若任何 mismatch 则 FAIL)
 */

#include "../include/int64_search.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <immintrin.h>

#define DATA_N        10000000ULL
#define QUERY_N       1000000ULL
#define HIT_RATIO     50
#define WARMUP_MS     500
#define REGRESSION_THRESHOLD  5000.0

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

/* ── 数据生成 ── */

static int cmp_int64(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

static int64_t *gen_uniform_sorted(size_t n, uint64_t seed) {
    int64_t *data = (int64_t *)malloc(n * sizeof(int64_t));
    if (!data) return NULL;
    uint64_t rng = seed;
    for (size_t i = 0; i < n; i++)
        data[i] = (int64_t)(xorshift64_next(&rng) & 0x7FFFFFFFFFFFFFFFULL);
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
            cand = (int64_t)(xorshift64_next(&rng) & 0x7FFFFFFFFFFFFFFFULL);
        } while (bsearch(&cand, data, d_n, sizeof(int64_t), cmp_int64) != NULL);
        queries[i] = cand;
    }
    return queries;
}

/* ── 计时 ── */

static unsigned long long rdtscp_bench(void) {
    unsigned int aux;
    unsigned long long t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

static unsigned long long estimate_cpu_freq(void) {
    unsigned long long t0 = rdtscp_bench();
    clock_t c0 = clock();
    while (((double)(clock() - c0) / CLOCKS_PER_SEC) < 0.1) {}
    unsigned long long t1 = rdtscp_bench();
    double elapsed_s = (double)(clock() - c0) / CLOCKS_PER_SEC;
    return (unsigned long long)((t1 - t0) / elapsed_s);
}

/* ── 主测试 ── */

int main(void) {
    printf("=== Int64 10M Uniform 性能回归测试 ===\n\n");

    /* 1. 生成数据 */
    printf("[1/4] 生成 10M int64 均匀数据 ...\n");
    int64_t *data = gen_uniform_sorted(DATA_N, 12345ULL);
    CHECK(data != NULL, "10M data generated");

    int64_t *queries = gen_queries(data, DATA_N, QUERY_N, HIT_RATIO, 67890ULL);
    CHECK(queries != NULL, "1M queries generated (50%% hit)");

    /* 2. 构建查找引擎 */
    printf("[2/4] 构建查找引擎 ...\n");
    int64_search_config_t cfg = {0, {0}};
    int64_search_t h = int64_search_create(data, DATA_N, &cfg);
    CHECK(h != NULL, "int64_search_create(10M) succeeded");

    /* 3. Warmup */
    printf("[3/4] Warmup %dms ...\n", WARMUP_MS);
    {
        volatile int discard = 0;
        unsigned long long t0 = rdtscp_bench();
        unsigned long long target = t0 + (unsigned long long)WARMUP_MS * 3000 * 1000;
        size_t i = 0;
        while (1) {
            size_t idx;
            discard |= int64_search_find(h, queries[i % QUERY_N], &idx);
            i++;
            if (i % 1024 == 0 && rdtscp_bench() >= target) break;
        }
        (void)discard;
    }

    /* 4. 计时查询 + 正确性验证 */
    printf("[4/4] 运行 1M 查询 (50%% hit) ...\n");
    {
        volatile int discard = 0;
        int mismatches = 0;

        unsigned long long t0 = rdtscp_bench();
        for (size_t q = 0; q < QUERY_N; q++) {
            size_t idx;
            int rc = int64_search_find(h, queries[q], &idx);
            if (rc == INT64_SEARCH_OK) {
                if (idx >= DATA_N || data[idx] != queries[q])
                    mismatches++;
            }
            discard |= rc;
        }
        unsigned long long t1 = rdtscp_bench();
        (void)discard;

        double cy_per_query = (double)(t1 - t0) / (double)QUERY_N;

        printf("\n=== 结果 ===\n");
        printf("  数据规模:      %llu (10M)\n", (unsigned long long)DATA_N);
        printf("  查询数:        %llu (1M)\n", (unsigned long long)QUERY_N);
        printf("  命中率:        %d%%\n", HIT_RATIO);
        printf("  API cy/query:  %.1f\n", cy_per_query);
        printf("  正确性:        %d mismatches\n", mismatches);

        CHECK(mismatches == 0, "correctness: 0 mismatches");

        if (cy_per_query <= REGRESSION_THRESHOLD) {
            printf("  PASS: cy/query %.1f ≤ threshold %.1f\n",
                   cy_per_query, REGRESSION_THRESHOLD);
        } else {
            fprintf(stderr, "  FAIL: cy/query %.1f > threshold %.1f\n",
                    cy_per_query, REGRESSION_THRESHOLD);
            g_failures++;
        }
    }

    /* 清理 */
    int64_search_destroy(h);
    free(queries);
    free(data);

    printf("\n=== %s ===\n", g_failures ? "FAILED" : "PASSED");
    return g_failures ? 1 : 0;
}
