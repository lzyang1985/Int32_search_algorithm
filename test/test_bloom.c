#include "../include/int32_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void test_bloom_false_positive_rate(void)
{
    size_t n = 1000000;
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    int32_t *test_keys = (int32_t *)malloc(n * sizeof(int32_t));
    size_t i;

    srand(12345);
    for (i = 0; i < n; i++) {
        data[i] = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
    }

    for (i = 0; i < n; i++) {
        test_keys[i] = (int32_t)(((uint32_t)rand() << 17) ^ (uint32_t)rand());
    }

    qsort(data, n, sizeof(int32_t), cmp_int);

    int32_search_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.use_bloom = 1;

    int32_search_t h = int32_search_create(data, n, &cfg);
    ASSERT(h != NULL, "bloom create");

    size_t fp_count = 0;
    size_t total_queries = n;

    for (i = 0; i < total_queries; i++) {
        size_t out_index;
        int rc = int32_search_find(h, test_keys[i], &out_index);
        if (rc == INT32_SEARCH_OK) {
            int found = 0;
            size_t lo = 0, hi = n;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (data[mid] < test_keys[i]) lo = mid + 1;
                else hi = mid;
            }
            if (lo < n && data[lo] == test_keys[i]) found = 1;
            if (!found) fp_count++;
        }
    }

    double fp_rate = (double)fp_count / (double)total_queries * 100.0;
    printf("  false positives: %zu / %zu = %.2f%%\n", fp_count, total_queries, fp_rate);
    ASSERT(fp_rate <= 2.0, "bloom fp rate %.2f%% exceeds 2%%", fp_rate);

    int32_search_destroy(h);
    free(data);
    free(test_keys);
}

static void test_bloom_no_false_negatives(void)
{
    size_t n = 100000;
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    size_t i;

    srand(67890);
    for (i = 0; i < n; i++) {
        data[i] = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
    }

    qsort(data, n, sizeof(int32_t), cmp_int);

    int32_search_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.use_bloom = 1;

    int32_search_t h = int32_search_create(data, n, &cfg);
    ASSERT(h != NULL, "bloom create for fn test");

    size_t fn_count = 0;
    for (i = 0; i < n; i++) {
        size_t out_index;
        int rc = int32_search_find(h, data[i], &out_index);
        if (rc != INT32_SEARCH_OK) {
            fn_count++;
        }
        if (rc == INT32_SEARCH_OK) {
            ASSERT(out_index == i || data[out_index] == data[i],
                   "bloom fn: correct result for inserted key i=%zu", i);
        }
    }
    ASSERT(fn_count == 0, "bloom false negatives: %zu", fn_count);
    printf("  false negatives: 0 / %zu (PASS)\n", n);

    int32_search_destroy(h);
    free(data);
}

static void test_bloom_no_config(void)
{
    size_t n = 1000;
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    size_t i;
    for (i = 0; i < n; i++) data[i] = (int32_t)(i * 2);

    int32_search_t h = int32_search_create(data, n, NULL);
    ASSERT(h != NULL, "bloom NULL config create");

    size_t out_index;
    int rc = int32_search_find(h, 500, &out_index);
    ASSERT(rc == INT32_SEARCH_OK, "bloom without config find");

    int32_search_destroy(h);
    free(data);
}

int main(void)
{
    printf("=== bloom filter test ===\n");

    test_bloom_no_config();
    printf("  no config: %s\n", failed ? "FAIL" : "PASS");
    int saved = failed; failed = 0;

    printf("  false negative test...\n");
    test_bloom_no_false_negatives();
    printf("  false negatives: %s\n", failed ? "FAIL" : "PASS");
    saved |= failed; failed = 0;

    printf("  false positive rate test (1M keys)...\n");
    test_bloom_false_positive_rate();
    printf("  false positives: %s\n", failed ? "FAIL" : "PASS");
    saved |= failed;

    return saved ? 1 : 0;
}
