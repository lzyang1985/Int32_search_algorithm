#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "int32_search.h"
#include "internal.h"

#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

#define TEST_PASS() printf("  PASS\n"); return 0

static int cmp_int32(const void *a, const void *b)
{
    int32_t ia = *(const int32_t *)a;
    int32_t ib = *(const int32_t *)b;
    return (ia > ib) - (ia < ib);
}

static int32_t rand32(void)
{
    return (int32_t)(((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 2) ^ (uint32_t)rand());
}

static int get_path(int32_search_t handle)
{
    int32_search_impl_t *impl = (int32_search_impl_t *)handle;
    return impl->path;
}

static int test_decision_uniform_100k(void)
{
    printf("test_decision_uniform_100k...");
    int n = 100000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = rand32();
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");
    ASSERT(get_path(h) == PATH_B1, "uniform 100k should select B1");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_decision_skewed_50pct(void)
{
    printf("test_decision_skewed_50pct...");
    int n = 10000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    uint16_t same_high = 0x1234;
    int half = n / 2;
    for (int i = 0; i < half; i++) data[i] = ((int32_t)same_high << 16) | (int32_t)(i * 2);
    for (int i = half; i < n; i++) data[i] = ((int32_t)((uint32_t)(i - half) << 17) ^ (int32_t)rand());
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");
    ASSERT(get_path(h) == PATH_A, "skewed 50pct should select A");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_decision_skewed_5pct(void)
{
    printf("test_decision_skewed_5pct...");
    int n = 20000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    uint16_t same_high = 0x1234;
    int same = n / 20;
    for (int i = 0; i < same; i++) data[i] = ((int32_t)same_high << 16) | (int32_t)(i * 2);
    for (int i = same; i < n; i++) data[i] = ((int32_t)((uint32_t)(i - same) << 17) ^ (int32_t)rand());
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");
    ASSERT(get_path(h) == PATH_B1, "5pct skewed should select B1");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_decision_max_bucket_boundary(void)
{
    printf("test_decision_max_bucket_boundary...");
    int n = 25000;
    int bucket_sz = 2000;
    int32_t *data_b1 = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    uint16_t h_base = 0x1000;
    for (int i = 0; i < bucket_sz; i++) data_b1[i] = ((int32_t)h_base << 16) | (int32_t)i;
    for (int i = bucket_sz; i < n; i++) data_b1[i] = (int32_t)((uint32_t)(h_base + 1 + (i - bucket_sz) / 3) << 16) | (int32_t)i;
    qsort(data_b1, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h1 = int32_search_create(data_b1, (size_t)n, NULL);
    ASSERT(h1 != NULL, "2000 bucket: create failed");
    ASSERT(get_path(h1) == PATH_B1, "max_bucket=2000 should select B1");
    int32_search_destroy(h1);

    int n_over = 3000;
    uint16_t high_over = 0x2000;
    int32_t *data_a = (int32_t *)malloc((size_t)n_over * sizeof(int32_t));
    for (int i = 0; i < n_over; i++) data_a[i] = ((int32_t)high_over << 16) | (int32_t)i;
    qsort(data_a, (size_t)n_over, sizeof(int32_t), cmp_int32);

    int32_search_t h2 = int32_search_create(data_a, (size_t)n_over, NULL);
    ASSERT(h2 != NULL, "3000 in 3000: create failed");
    ASSERT(get_path(h2) == PATH_A, "max_bucket > n/10 should select A");
    int32_search_destroy(h2);

    free(data_b1);
    free(data_a);
    TEST_PASS();
}

static int test_decision_small_n(void)
{
    printf("test_decision_small_n...");
    int n = 100;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = rand32();
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "small n: create failed");
    ASSERT(get_path(h) == PATH_B1, "small n uniform should select B1");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_decision_null_data(void)
{
    printf("test_decision_null_data...");
    int32_search_t h = int32_search_create(NULL, 100, NULL);
    ASSERT(h == NULL, "NULL data should return NULL");
    h = int32_search_create((const int32_t *)&(int32_t){1}, 0, NULL);
    ASSERT(h == NULL, "n=0 should return NULL");
    TEST_PASS();
}

int main(void)
{
    int failures = 0;
    failures += test_decision_uniform_100k();
    failures += test_decision_skewed_50pct();
    failures += test_decision_skewed_5pct();
    failures += test_decision_max_bucket_boundary();
    failures += test_decision_small_n();
    failures += test_decision_null_data();
    printf("\n=== B1 Decision: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
