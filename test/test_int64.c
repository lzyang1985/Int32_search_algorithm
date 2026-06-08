#include "../include/int64_search.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
        g_failures++; \
    } else { \
        printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

static uint64_t xorshift64_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static int cmp_int64(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

static int64_t *gen_uniform(size_t n, uint64_t seed) {
    int64_t *data = (int64_t *)malloc(n * sizeof(int64_t));
    if (!data) return NULL;
    uint64_t rng = seed;
    for (size_t i = 0; i < n; i++)
        data[i] = (int64_t)(xorshift64_next(&rng) & 0x7FFFFFFFFFFFFFFFULL);
    qsort(data, n, sizeof(int64_t), cmp_int64);
    return data;
}

static void test_L1_interface(void) {
    printf("\n=== L1: 接口契约 ===\n");

    int64_search_config_t cfg = {0, {0}};
    (void)cfg;

    int64_search_t h = int64_search_create(NULL, 0, NULL);
    CHECK(h == NULL, "create(NULL,0) returns NULL");

    h = int64_search_create(NULL, 100, NULL);
    CHECK(h == NULL, "create(NULL,100) returns NULL");

    int result = int64_search_find(NULL, 42, NULL);
    CHECK(result == INT64_SEARCH_ERR_NULL_HANDLE, "find(NULL) = ERR_NULL_HANDLE");

    result = int64_search_destroy(NULL);
    CHECK(result == INT64_SEARCH_OK, "destroy(NULL) = OK (idempotent)");

    result = int64_search_set_bloom_bypass(NULL, 1);
    CHECK(result == INT64_SEARCH_ERR_NULL_HANDLE, "set_bloom_bypass(NULL) = ERR_NULL_HANDLE");

    result = int64_search_get_bloom_bypass(NULL);
    CHECK(result == INT64_SEARCH_ERR_NULL_HANDLE, "get_bloom_bypass(NULL) = ERR_NULL_HANDLE");

    const char *ver = int64_search_version();
    CHECK(ver != NULL && strstr(ver, "libint64search") != NULL, "version = %s", ver);
}

static void test_L2_correctness(void) {
    printf("\n=== L2: 正确性验证 ===\n");

    static const size_t sizes[] = {0, 1, 2, 3, 4, 5, 16, 64, 256, 1024, 10000};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int si = 0; si < num_sizes; si++) {
        size_t n = sizes[si];
        int64_t *data = gen_uniform(n, (uint64_t)(n * 2654435761ULL));
        if (!data && n > 0) { g_failures++; continue; }

        int64_search_config_t cfg = {0, {0}};
        int64_search_t h = int64_search_create(data, n, &cfg);
        if (n == 0) {
            CHECK(h == NULL, "n=%zu: create returns NULL (expected)", n);
            free(data);
            continue;
        }
        CHECK(h != NULL, "n=%zu: create succeeded", n);

        uint64_t rng = 42;
        int mismatches = 0;
        for (int q = 0; q < 500; q++) {
            int64_t key = data[(size_t)(xorshift64_next(&rng) % n)];
            size_t idx;
            int rc = int64_search_find(h, key, &idx);
            if (rc != 0 || idx >= n || data[idx] != key) mismatches++;
        }
        CHECK(mismatches == 0, "n=%zu: 500 queries all correct (%d mismatches)", n, mismatches);

        int64_search_destroy(h);
        free(data);
    }
}

static void test_L3_path_decision(void) {
    printf("\n=== L3: 路径决策验证 ===\n");

    int64_t *data1 = gen_uniform(100000, 12345);
    CHECK(data1 != NULL, "uniform 100K generated");

    int64_search_config_t cfg = {0, {0}};
    int64_search_t h1 = int64_search_create(data1, 100000, &cfg);
    CHECK(h1 != NULL, "uniform 100K: create succeeded");

    int64_t key = data1[50000];
    size_t idx;
    int rc = int64_search_find(h1, key, &idx);
    CHECK(rc == 0 && data1[idx] == key, "uniform: find returns correct index");
    int64_search_destroy(h1);
    free(data1);
}

static void test_L4_bloom_bypass(void) {
    printf("\n=== L4: Bloom Bypass ===\n");

    int64_t *data = gen_uniform(10000, 7777);
    CHECK(data != NULL, "data generated");

    int64_search_config_t cfg = {0, {0}};
    int64_search_t h = int64_search_create(data, 10000, &cfg);
    CHECK(h != NULL, "create succeeded");

    int rc = int64_search_set_bloom_bypass(h, 1);
    CHECK(rc == INT64_SEARCH_OK, "set_bloom_bypass(1) = OK");

    int bypass = int64_search_get_bloom_bypass(h);
    CHECK(bypass == 1, "get_bloom_bypass returns 1");

    rc = int64_search_set_bloom_bypass(h, 0);
    CHECK(rc == INT64_SEARCH_OK, "set_bloom_bypass(0) = OK");
    bypass = int64_search_get_bloom_bypass(h);
    CHECK(bypass == 0, "get_bloom_bypass returns 0");

    int64_t key = data[5000];
    size_t idx;
    rc = int64_search_find(h, key, &idx);
    CHECK(rc == 0 && data[idx] == key, "find with bypass=0 works");

    int64_search_set_bloom_bypass(h, 1);
    size_t idx2;
    rc = int64_search_find(h, key, &idx2);
    CHECK(rc == 0 && idx == idx2, "find with bypass=1 returns same index");

    int64_search_destroy(h);
    free(data);
}

static void test_L5_rebuild(void) {
    printf("\n=== L5: Rebuild ===\n");

    int64_t *d1 = gen_uniform(5000, 111);
    int64_t *d2 = gen_uniform(8000, 222);
    CHECK(d1 != NULL && d2 != NULL, "data generated");

    int64_search_config_t cfg = {0, {0}};
    int64_search_t h = int64_search_create(d1, 5000, &cfg);
    CHECK(h != NULL, "create d1 succeeded");

    int rc = int64_search_rebuild(h, d2, 8000);
    CHECK(rc == INT64_SEARCH_OK, "rebuild d2 succeeded");

    int64_t key = d2[4000];
    size_t idx;
    rc = int64_search_find(h, key, &idx);
    CHECK(rc == 0 && d2[idx] == key, "find after rebuild works on new data");

    int64_search_destroy(h);
    free(d1); free(d2);
}

static void test_L6_boundary(void) {
    printf("\n=== L6: 边界值 ===\n");

    int64_t data_arr[] = {INT64_MIN, (int64_t)-1, (int64_t)0, (int64_t)1, INT64_MAX};
    size_t n = sizeof(data_arr) / sizeof(data_arr[0]);

    int64_search_config_t cfg = {0, {0}};
    int64_search_t h = int64_search_create(data_arr, n, &cfg);
    CHECK(h != NULL, "boundary: create succeeded");

    int64_t targets[] = {INT64_MIN, INT64_MIN + 1, (int64_t)-2, (int64_t)-1,
                         (int64_t)0, (int64_t)1, (int64_t)2,
                         INT64_MAX - 1, INT64_MAX};
    int num_t = sizeof(targets) / sizeof(targets[0]);

    int mismatches = 0;
    for (int i = 0; i < num_t; i++) {
        int64_t t = targets[i];
        size_t idx;
        int rc = int64_search_find(h, t, &idx);

        int found = 0;
        size_t expected = (size_t)-1;
        for (size_t j = 0; j < n; j++) {
            if (data_arr[j] == t) { found = 1; expected = j; break; }
        }

        if ((found && rc != 0) || (!found && rc == 0) || (found && idx != expected))
            mismatches++;
    }
    CHECK(mismatches == 0, "boundary: %d targets, %d mismatches", num_t, mismatches);

    int64_search_destroy(h);
}

static void test_L7_cow_behavior(void) {
    printf("\n=== L7: COW 行为 (Phase 2) ===\n");

    /* === L7-COW-1: rebuild 后查询旧 key → NOT_FOUND === */
    {
        int64_t data1[100], data2[100];
        for (int i = 0; i < 100; i++) { data1[i] = (int64_t)i; data2[i] = (int64_t)(10000 + i); }

        int64_search_config_t cfg = {0, {0}};
        int64_search_t h = int64_search_create(data1, 100, &cfg);
        CHECK(h != NULL, "L7-COW-1: create data1 succeeded");

        int rc = int64_search_rebuild(h, data2, 100);
        CHECK(rc == INT64_SEARCH_OK, "L7-COW-1: rebuild data2 succeeded");

        size_t idx;
        rc = int64_search_find(h, 50, &idx);
        CHECK(rc == INT64_SEARCH_ERR_NOT_FOUND, "L7-COW-1: old key=50 NOT_FOUND after rebuild");

        int64_search_destroy(h);
    }

    /* === L7-COW-2: rebuild 后查询新 key → OK === */
    {
        int64_t data1[100], data2[100];
        for (int i = 0; i < 100; i++) { data1[i] = (int64_t)i; data2[i] = (int64_t)(10000 + i); }

        int64_search_config_t cfg = {0, {0}};
        int64_search_t h = int64_search_create(data1, 100, &cfg);
        CHECK(h != NULL, "L7-COW-2: create data1 succeeded");

        int rc = int64_search_rebuild(h, data2, 100);
        CHECK(rc == INT64_SEARCH_OK, "L7-COW-2: rebuild data2 succeeded");

        size_t idx;
        rc = int64_search_find(h, 10050, &idx);
        CHECK(rc == INT64_SEARCH_OK && idx == 50, "L7-COW-2: new key=10050 at idx=50 after rebuild");

        int64_search_destroy(h);
    }

    /* === L7-COW-3: rebuild 100 次后查询正确(轻量版 DEV-I64-001 验证) === */
    {
        int64_t data[5000];
        for (int i = 0; i < 5000; i++) data[i] = (int64_t)i;

        int64_search_config_t cfg = {0, {0}};
        int64_search_t h = int64_search_create(data, 5000, &cfg);
        CHECK(h != NULL, "L7-COW-3: create succeeded");

        int mismatches = 0;
        for (int round = 0; round < 100; round++) {
            int64_t base = (int64_t)(round * 10000);
            for (int i = 0; i < 5000; i++) data[i] = base + (int64_t)i;

            int rc = int64_search_rebuild(h, data, 5000);
            if (rc != INT64_SEARCH_OK) { mismatches++; continue; }

            size_t idx;
            int64_t key = base + 2500;
            rc = int64_search_find(h, key, &idx);
            if (rc != 0 || idx != 2500) mismatches++;
        }
        CHECK(mismatches == 0, "L7-COW-3: 100 rebuild rounds, %d mismatches", mismatches);

        int64_search_destroy(h);
    }

    /* === L7-COW-4: rebuild 保留 bloom_bypass 状态(Q-A2) === */
    {
        int64_t data1[100], data2[100];
        for (int i = 0; i < 100; i++) { data1[i] = (int64_t)i; data2[i] = (int64_t)(20000 + i); }

        int64_search_config_t cfg = {0, {0}};
        int64_search_t h = int64_search_create(data1, 100, &cfg);
        CHECK(h != NULL, "L7-COW-4: create succeeded");

        /* 开启 bypass */
        int rc = int64_search_set_bloom_bypass(h, 1);
        CHECK(rc == INT64_SEARCH_OK, "L7-COW-4: set_bloom_bypass(1) OK");

        /* rebuild */
        rc = int64_search_rebuild(h, data2, 100);
        CHECK(rc == INT64_SEARCH_OK, "L7-COW-4: rebuild OK");

        /* 验证 bypass 保持 */
        int bypass = int64_search_get_bloom_bypass(h);
        CHECK(bypass == 1, "L7-COW-4: bloom_bypass still 1 after rebuild");

        /* 关闭 */
        int64_search_set_bloom_bypass(h, 0);
        rc = int64_search_rebuild(h, data1, 100);
        CHECK(rc == INT64_SEARCH_OK, "L7-COW-4: rebuild data1 again OK");
        bypass = int64_search_get_bloom_bypass(h);
        CHECK(bypass == 0, "L7-COW-4: bloom_bypass 0 preserved");

        int64_search_destroy(h);
    }

    /* === L7-COW-5: destroy 幂等性 + 等待语义(单线程验证) === */
    {
        int64_t data[100];
        for (int i = 0; i < 100; i++) data[i] = (int64_t)i;

        int64_search_config_t cfg = {0, {0}};
        int64_search_t h = int64_search_create(data, 100, &cfg);
        CHECK(h != NULL, "L7-COW-5: create succeeded");

        int rc = int64_search_destroy(h);
        CHECK(rc == INT64_SEARCH_OK, "L7-COW-5: destroy 1st OK");

        /* Phase 2 设计: destroy 后的 handle 行为未定义(impl 已 free)
         * Phase 1 行为: destroy(NULL) 返回 OK(idempotent) */
        rc = int64_search_destroy(NULL);
        CHECK(rc == INT64_SEARCH_OK, "L7-COW-5: destroy(NULL) OK (idempotent)");
    }
}

int main(void) {
    printf("=== int64_search 测试套件 ===\n");

    test_L1_interface();
    test_L2_correctness();
    test_L3_path_decision();
    test_L4_bloom_bypass();
    test_L5_rebuild();
    test_L6_boundary();
    test_L7_cow_behavior();

    printf("\n=== 结果: %d failures ===\n", g_failures);
    return g_failures ? 1 : 0;
}
