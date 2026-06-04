#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "int32_search.h"

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

static int test_b1_n0(void)
{
    printf("test_b1_n0...");
    int32_t data[] = {1};
    int32_search_t h = int32_search_create(data, 0, NULL);
    ASSERT(h == NULL, "n=0 should return NULL");
    int32_search_destroy(h);
    TEST_PASS();
}

static int test_b1_n1_hit(void)
{
    printf("test_b1_n1_hit...");
    int32_t data[] = {42};
    int32_search_t h = int32_search_create(data, 1, NULL);
    ASSERT(h != NULL, "create n=1 failed");
    size_t idx;
    int rc = int32_search_find(h, 42, &idx);
    ASSERT(rc == INT32_SEARCH_OK, "n=1 hit fail");
    ASSERT(idx == 0, "n=1 idx mismatch");
    int32_search_destroy(h);
    TEST_PASS();
}

static int test_b1_n1_miss(void)
{
    printf("test_b1_n1_miss...");
    int32_t data[] = {42};
    int32_search_t h = int32_search_create(data, 1, NULL);
    ASSERT(h != NULL, "create n=1 failed");
    size_t idx;
    int rc = int32_search_find(h, 0, &idx);
    ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "n=1 miss fail");
    rc = int32_search_find(h, 100, &idx);
    ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "n=1 miss high fail");
    int32_search_destroy(h);
    TEST_PASS();
}

static int test_b1_small_n_range(int n)
{
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = (int32_t)i * 3;
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create small n failed");

    for (int i = 0; i < n; i++) {
        size_t idx;
        int rc = int32_search_find(h, data[i], &idx);
        ASSERT(rc == INT32_SEARCH_OK, "small n hit fail");
        ASSERT(data[idx] == data[i], "small n value mismatch");
    }

    int32_t miss_keys[] = { -100, -1, 1000000, INT32_MAX };
    for (int i = 0; i < 4; i++) {
        int found = 0;
        for (int j = 0; j < n; j++) { if (data[j] == miss_keys[i]) found = 1; }
        if (found) continue;
        size_t idx;
        int rc = int32_search_find(h, miss_keys[i], &idx);
        ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "small n miss fail");
    }

    int32_search_destroy(h);
    free(data);
    return 0;
}

static int test_b1_n2_to_n16(void)
{
    printf("test_b1_n2_to_n16...");
    for (int n = 2; n <= 16; n++) {
        if (test_b1_small_n_range(n) != 0) return 1;
    }
    TEST_PASS();
}

static int test_b1_n17_to_n64(void)
{
    printf("test_b1_n17_to_n64...");
    for (int n = 17; n <= 64; n++) {
        if (test_b1_small_n_range(n) != 0) return 1;
    }
    TEST_PASS();
}

static int test_b1_empty_bucket(void)
{
    printf("test_b1_empty_bucket...");
    int n = 1000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = (int32_t)(i + 100000);
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    int32_t miss_keys[] = { 0, 1, 99999, INT32_MIN };
    for (int i = 0; i < 4; i++) {
        size_t idx;
        int rc = int32_search_find(h, miss_keys[i], &idx);
        ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "empty bucket miss fail");
    }

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_single_bucket(void)
{
    printf("test_b1_single_bucket...");
    int n = 1000;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    uint16_t same_high = 0x1234;
    for (int i = 0; i < n; i++) data[i] = ((int32_t)same_high << 16) | (int32_t)i;
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "single bucket: create failed");

    for (int i = 0; i < n; i++) {
        size_t idx;
        int rc = int32_search_find(h, data[i], &idx);
        ASSERT(rc == INT32_SEARCH_OK, "single bucket hit fail");
        ASSERT(data[idx] == data[i], "single bucket value mismatch");
    }

    int32_t miss_key = ((int32_t)(same_high + 1) << 16);
    size_t idx;
    int rc = int32_search_find(h, miss_key, &idx);
    ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "single bucket miss fail");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_all_buckets(void)
{
    printf("test_b1_all_buckets...");
    int n = 65536;
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = (int32_t)((uint32_t)i << 16) | 1;
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "all buckets: create failed");

    for (int i = 0; i < n; i += 1000) {
        size_t idx;
        int rc = int32_search_find(h, data[i], &idx);
        ASSERT(rc == INT32_SEARCH_OK, "all buckets hit fail");
    }

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_first_last_bucket(void)
{
    printf("test_b1_first_last_bucket...");
    int32_t data[] = { 0x00010000, 0x00010001, 0xFFFF0000, 0xFFFF0001 };
    int n = 4;

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "first/last: create failed");

    size_t idx;
    ASSERT(int32_search_find(h, 0x00010000, &idx) == INT32_SEARCH_OK, "bucket pos fail");
    ASSERT(idx < (size_t)n, "bucket pos idx range");
    ASSERT(int32_search_find(h, 0xFFFF0000, &idx) == INT32_SEARCH_OK, "bucket neg fail");
    ASSERT(idx < (size_t)n, "bucket neg idx range");

    int32_search_destroy(h);
    TEST_PASS();
}

static int test_b1_out_of_range(void)
{
    printf("test_b1_out_of_range...");
    int32_t data[] = { 0x00010000, 0x00010001 };
    int n = 2;

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "oor: create failed");

    size_t idx;
    int32_t keys[] = { 0xFFFF0000, 0xFFFF0001, 0 };
    for (int i = 0; i < 3; i++) {
        int rc = int32_search_find(h, keys[i], &idx);
        ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "oor miss fail");
    }

    int32_search_destroy(h);
    TEST_PASS();
}

static int test_b1_null_handle(void)
{
    printf("test_b1_null_handle...");
    size_t idx;
    ASSERT(int32_search_find(NULL, 42, &idx) == INT32_SEARCH_ERR_NULL_HANDLE, "null handle");
    ASSERT(int32_search_destroy(NULL) == INT32_SEARCH_OK, "destroy null");
    TEST_PASS();
}

int main(void)
{
    int failures = 0;
    failures += test_b1_n0();
    failures += test_b1_n1_hit();
    failures += test_b1_n1_miss();
    failures += test_b1_n2_to_n16();
    failures += test_b1_n17_to_n64();
    failures += test_b1_empty_bucket();
    failures += test_b1_single_bucket();
    failures += test_b1_all_buckets();
    failures += test_b1_first_last_bucket();
    failures += test_b1_out_of_range();
    failures += test_b1_null_handle();
    printf("\n=== B1 Boundary: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
