#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "int32_search.h"

#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

#define TEST_PASS() printf("  PASS\n"); return 0

static int32_t rand32(void)
{
    return (int32_t)(((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 2) ^ (uint32_t)rand());
}

static int cmp_int32(const void *a, const void *b)
{
    int32_t ia = *(const int32_t *)a;
    int32_t ib = *(const int32_t *)b;
    return (ia > ib) - (ia < ib);
}

static int test_b1_vs_a_hit_10k(void)
{
    printf("test_b1_vs_a_hit_10k...");
    int n = 10000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = rand32();
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    for (int i = 0; i < 1000; i++) {
        int idx = rand() % n;
        size_t out_idx;
        int rc = int32_search_find(h, data[idx], &out_idx);
        ASSERT(rc == INT32_SEARCH_OK, "b1 hit: should return OK");
        ASSERT(data[out_idx] == data[idx], "b1 hit: value mismatch");
    }

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_vs_a_miss_10k(void)
{
    printf("test_b1_vs_a_miss_10k...");
    int n = 10000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = rand32();
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    for (int i = 0; i < 1000; i++) {
        int32_t key = rand32();
        int found = 0;
        for (int j = 0; j < n; j++) { if (data[j] == key) { found = 1; break; } }
        if (found) continue;

        size_t out_idx;
        int rc = int32_search_find(h, key, &out_idx);
        ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "b1 miss: should return NOT_FOUND");
    }

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_vs_bsearch_100k(void)
{
    printf("test_b1_vs_bsearch_100k...");
    int n = 100000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = rand32();
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    for (int i = 0; i < 5000; i++) {
        int32_t key = rand32();
        size_t out_idx;
        int rc = int32_search_find(h, key, &out_idx);

        void *bs = bsearch(&key, data, (size_t)n, sizeof(int32_t), cmp_int32);
        if (bs != NULL) {
            ASSERT(rc == INT32_SEARCH_OK, "bsearch hit, b1 should hit");
            ASSERT(data[out_idx] == key, "b1 value mismatch vs bsearch");
        } else {
            ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "bsearch miss, b1 should miss");
        }
    }

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_negative_keys(void)
{
    printf("test_b1_negative_keys...");
    int n = 1000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = rand32();
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    for (int i = 0; i < 500; i++) {
        int idx = rand() % n;
        size_t out_idx;
        int rc = int32_search_find(h, data[idx], &out_idx);
        ASSERT(rc == INT32_SEARCH_OK, "negative hit fail");
        ASSERT(data[out_idx] == data[idx], "negative value mismatch");
    }

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_extreme_values(void)
{
    printf("test_b1_extreme_values...");
    int n = 8;
    int32_t data[] = { INT32_MIN, -1, 0, 1, 100, 1000, 65536, INT32_MAX };

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    int32_t test_keys[] = { INT32_MIN, -1, 0, 1, 100, 1000, 65536, INT32_MAX, -999, 65537 };
    int     expected_hit[] = { 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 };

    for (int i = 0; i < 10; i++) {
        size_t out_idx;
        int rc = int32_search_find(h, test_keys[i], &out_idx);
        if (expected_hit[i]) {
            ASSERT(rc == INT32_SEARCH_OK, "extreme: should hit");
            ASSERT(data[out_idx] == test_keys[i], "extreme: value mismatch");
        } else {
            ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "extreme: should miss");
        }
    }

    int32_search_destroy(h);
    TEST_PASS();
}

static int test_version(void)
{
    printf("test_version...");
    const char *v = int32_search_version();
    ASSERT(v != NULL, "version is NULL");
    ASSERT(strcmp(v, "libint32search 1.0.0") == 0, "version mismatch");
    TEST_PASS();
}

int main(void)
{
    srand((unsigned)time(NULL));
    int failures = 0;
    failures += test_version();
    failures += test_b1_vs_a_hit_10k();
    failures += test_b1_vs_a_miss_10k();
    failures += test_b1_vs_bsearch_100k();
    failures += test_b1_negative_keys();
    failures += test_b1_extreme_values();
    printf("\n=== B1 Correctness: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
