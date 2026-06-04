#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "int32_search.h"

static int compare_int32(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static int run_test(size_t n, int hit_percent)
{
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    if (data == NULL) { printf("  malloc failed\n"); return 1; }

    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)((unsigned int)rand() ^ ((unsigned int)rand() << 15));

    int32_search_t h = int32_search_create(data, n, NULL);
    if (h == NULL) { printf("  create failed\n"); free(data); return 1; }

    int32_t *sorted = (int32_t *)malloc(n * sizeof(int32_t));
    memcpy(sorted, data, n * sizeof(int32_t));
    qsort(sorted, n, sizeof(int32_t), compare_int32);

    size_t num_queries = 100000;
    size_t hit_count = (size_t)((unsigned long long)num_queries * hit_percent / 100);

    int mismatches = 0;
    for (size_t i = 0; i < num_queries; i++) {
        int32_t key;
        if (i < hit_count) {
            size_t r = (size_t)rand() % n;
            key = sorted[r];
        } else {
            int32_t candidate;
            int tries = 0;
            do {
                candidate = (int32_t)((unsigned int)rand() ^ ((unsigned int)rand() << 15));
                if (++tries > 100) break;
            } while (bsearch(&candidate, sorted, n, sizeof(int32_t), compare_int32) != NULL);
            key = candidate;
        }

        size_t out_idx = (size_t)-1;
        int ret = int32_search_find(h, key, &out_idx);

        void *bs_result = bsearch(&key, sorted, n, sizeof(int32_t), compare_int32);

        if (bs_result != NULL) {
            if (ret != INT32_SEARCH_OK) {
                mismatches++;
                if (mismatches <= 3) {
                    printf("  MISMATCH: key=%d found by bsearch, lib returns %d\n",
                           key, ret);
                }
            } else if (sorted[out_idx] != key) {
                mismatches++;
                if (mismatches <= 3) {
                    printf("  MISMATCH: key=%d, lib idx=%zu has value %d\n",
                           key, out_idx, (int)sorted[out_idx]);
                }
            } else if (out_idx > 0 && sorted[out_idx - 1] == key) {
                mismatches++;
                if (mismatches <= 3) {
                    printf("  MISMATCH: key=%d, lib idx=%zu not first (idx-1 also matches)\n",
                           key, out_idx);
                }
            }
        } else {
            if (ret != INT32_SEARCH_ERR_NOT_FOUND) {
                mismatches++;
                if (mismatches <= 3) {
                    printf("  MISMATCH: key=%d, bsearch miss, lib returns %d (idx=%zu)\n",
                           key, ret, out_idx);
                }
            }
        }
    }

    int32_search_destroy(h);
    free(sorted);
    free(data);

    if (mismatches > 0) {
        printf("  FAIL: %d mismatches / %zu queries\n", mismatches, num_queries);
        return 1;
    }
    printf("  PASS: 0 mismatches / %zu queries\n", num_queries);
    return 0;
}

int main(void)
{
    srand((unsigned int)time(NULL));

    printf("Correctness Tests\n=================\n\n");

    int failures = 0;

    printf("n=100, 50%% hit: ");
    failures += run_test(100, 50);

    printf("n=10000, 50%% hit: ");
    failures += run_test(10000, 50);

    printf("n=10000, 100%% hit: ");
    failures += run_test(10000, 100);

    printf("n=10000, 0%% hit: ");
    failures += run_test(10000, 0);

    printf("n=100000, 50%% hit: ");
    failures += run_test(100000, 50);

    printf("\nTotal failures: %d\n", failures);
    return failures > 0 ? 1 : 0;
}
