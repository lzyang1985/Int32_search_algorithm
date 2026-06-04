#include "../include/int32_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int failed = 0;

#define ASSERT(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: " fmt "\n", ##__VA_ARGS__); \
        failed = 1; \
    } \
} while(0)

static int cmp_int(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static void test_range_empty(void)
{
    int32_t data[] = {10, 20, 30, 40, 50};
    int32_search_t h = int32_search_create(data, 5, NULL);
    ASSERT(h != NULL, "create empty test handle");

    size_t first, count;
    int rc;

    rc = int32_search_find_range(h, 0, 5, &first, &count);
    ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "empty range below");
    ASSERT(count == 0, "empty range below count=0");

    rc = int32_search_find_range(h, 55, 100, &first, &count);
    ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "empty range above");
    ASSERT(first == 5, "empty range above first=5");
    ASSERT(count == 0, "empty range above count=0");

    rc = int32_search_find_range(h, 15, 18, &first, &count);
    ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "empty range gap");

    int32_search_destroy(h);
}

static void test_range_single(void)
{
    int32_t data[] = {42};
    int32_search_t h = int32_search_create(data, 1, NULL);
    ASSERT(h != NULL, "create single test handle");

    size_t first, count;
    int rc;

    rc = int32_search_find_range(h, 42, 42, &first, &count);
    ASSERT(rc == INT32_SEARCH_OK, "single match");
    ASSERT(first == 0, "single match first=0");
    ASSERT(count == 1, "single match count=1");

    rc = int32_search_find_range(h, 0, 100, &first, &count);
    ASSERT(rc == INT32_SEARCH_OK, "single full range match");
    ASSERT(first == 0 && count == 1, "single full range result");

    rc = int32_search_find_range(h, -100, 0, &first, &count);
    ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "single miss below");

    int32_search_destroy(h);
}

static void test_range_zero_elements(void)
{
    int32_t data[] = {10};
    int32_search_t h = int32_search_create(data, 1, NULL);
    ASSERT(h != NULL, "create zero test handle");

    size_t first, count;
    int rc;

    rc = int32_search_find_range(h, 30, 20, &first, &count);
    ASSERT(rc == INT32_SEARCH_ERR_INVALID_ARG, "low > high");

    rc = int32_search_find_range(NULL, 0, 10, &first, &count);
    ASSERT(rc == INT32_SEARCH_ERR_NULL_HANDLE, "null handle");

    rc = int32_search_find_range(h, 0, 10, NULL, &count);
    ASSERT(rc == INT32_SEARCH_ERR_INVALID_ARG, "null out_first");

    rc = int32_search_find_range(h, 0, 10, &first, NULL);
    ASSERT(rc == INT32_SEARCH_ERR_INVALID_ARG, "null out_count");

    int32_search_destroy(h);
}

static void test_range_boundary(void)
{
    int i;
    for (int n = 0; n <= 64; n++) {
        int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
        if (n > 0) {
            for (i = 0; i < n; i++) data[i] = i * 10;
        }

        int32_search_t h = int32_search_create(data, (size_t)n, NULL);
        if (n == 0) {
            ASSERT(h == NULL, "create n=0 returns NULL");
            free(data);
            continue;
        }
        ASSERT(h != NULL, "create boundary n=%d", n);

        size_t first, count;
        int rc;

        rc = int32_search_find_range(h, 0, (int32_t)((n - 1) * 10), &first, &count);
        ASSERT(rc == INT32_SEARCH_OK, "boundary full range n=%d", n);
        ASSERT(first == 0 && count == (size_t)n, "boundary full range n=%d result", n);

        if (n >= 2) {
            rc = int32_search_find_range(h, 10, (int32_t)((n - 1) * 10), &first, &count);
            ASSERT(rc == INT32_SEARCH_OK, "boundary partial n=%d", n);
            ASSERT(first == 1 && count == (size_t)(n - 1), "boundary partial n=%d result", n);
        }

        rc = int32_search_find_range(h, -100, -1, &first, &count);
        ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "boundary below n=%d", n);

        rc = int32_search_find_range(h, (int32_t)(n * 10), (int32_t)(n * 10 + 100), &first, &count);
        ASSERT(rc == INT32_SEARCH_ERR_NOT_FOUND, "boundary above n=%d", n);

        int32_search_destroy(h);
        free(data);
    }
}

static void test_range_random(int count)
{
    srand(42);

    for (int sz = 1; sz <= 10; sz++) {
        size_t n = (size_t)sz * 100000;
        int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
        int i;
        for (i = 0; i < (int)n; i++) {
            data[i] = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
        }
        qsort(data, n, sizeof(int32_t), cmp_int);

        int32_search_t h = int32_search_create(data, n, NULL);
        ASSERT(h != NULL, "create random n=%zu", n);

        for (i = 0; i < count / 10; i++) {
            int32_t low  = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
            int32_t high = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
            if (low > high) { int32_t t = low; low = high; high = t; }

            size_t first, cnt;
            int rc = int32_search_find_range(h, low, high, &first, &cnt);
            ASSERT(rc == INT32_SEARCH_OK || rc == INT32_SEARCH_ERR_NOT_FOUND,
                   "random rc valid n=%zu i=%d", n, i);

            size_t expected_first = 0;
            while (expected_first < n && data[expected_first] < low) expected_first++;
            size_t expected_last = 0;
            while (expected_last < n && data[expected_last] <= high) expected_last++;

            ASSERT(first == expected_first,
                   "random first mismatch n=%zu i=%d got=%zu exp=%zu low=%d high=%d",
                   n, i, first, expected_first, low, high);
            ASSERT(cnt == (expected_last - expected_first),
                   "random count mismatch n=%zu i=%d got=%zu exp=%zu low=%d high=%d",
                   n, i, cnt, expected_last - expected_first, low, high);
        }

        int32_search_destroy(h);
        free(data);
    }
}

int main(void)
{
    printf("=== find_range test ===\n");

    test_range_empty();
    printf("  empty: %s\n", failed ? "FAIL" : "PASS");
    int saved = failed; failed = 0;

    test_range_single();
    printf("  single: %s\n", failed ? "FAIL" : "PASS");
    saved |= failed; failed = 0;

    test_range_zero_elements();
    printf("  zero/error: %s\n", failed ? "FAIL" : "PASS");
    saved |= failed; failed = 0;

    test_range_boundary();
    printf("  boundary n=0..64: %s\n", failed ? "FAIL" : "PASS");
    saved |= failed; failed = 0;

    printf("  random 1M cross-validation...\n");
    test_range_random(100000);
    printf("  random: %s\n", failed ? "FAIL" : "PASS");
    saved |= failed;

    return saved ? 1 : 0;
}
