#include "../include/int32_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int cmp_int(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

int main(void)
{
    size_t n = 10000000;
    size_t test_n = 1000000;
    size_t i;

    printf("=== 10M Bloom False Positive Rate Test ===\n");
    printf("Allocating %zu int32 keys (~38 MB)...\n", n);

    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    int32_t *test_keys = (int32_t *)malloc(test_n * sizeof(int32_t));
    if (!data || !test_keys) { printf("malloc failed\n"); return 1; }

    srand(12345);
    printf("Generating 10M random keys...\n");
    for (i = 0; i < n; i++)
        data[i] = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());

    printf("Generating 1M test keys...\n");
    for (i = 0; i < test_n; i++)
        test_keys[i] = (int32_t)(((uint32_t)rand() << 17) ^ (uint32_t)rand());

    printf("Sorting 10M keys...\n");
    qsort(data, n, sizeof(int32_t), cmp_int);

    int32_search_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.use_bloom = 1;

    printf("Creating search structure with bloom (m/n=12, k=3)...\n");
    int32_search_t h = int32_search_create(data, n, &cfg);
    if (!h) { printf("create failed\n"); return 1; }

    printf("Running 1M bloom queries...\n");
    size_t fp_count = 0;
    for (i = 0; i < test_n; i++) {
        size_t out_index;
        int rc = int32_search_find(h, test_keys[i], &out_index);
        if (rc == INT32_SEARCH_OK) {
            size_t lo = 0, hi = n;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (data[mid] < test_keys[i]) lo = mid + 1;
                else hi = mid;
            }
            if (!(lo < n && data[lo] == test_keys[i])) fp_count++;
        }
    }

    double fp_rate = (double)fp_count / (double)test_n * 100.0;
    printf("\nFalse positives: %zu / %zu = %.3f%%\n", fp_count, test_n, fp_rate);
    printf(fp_rate <= 1.0 ? "PASS: fp_rate <= 1%%\n" : "NOTE: fp_rate > 1%%\n");

    int32_search_destroy(h);
    free(data);
    free(test_keys);
    return 0;
}
