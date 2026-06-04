#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "int32_search.h"

static const size_t test_sizes[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64
};
static const size_t num_sizes = 18;

static int run_boundary_test(size_t n)
{
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    if (data == NULL && n > 0) return 1;

    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)(i * 10);

    int32_search_t h;
    if (n == 0) {
        h = int32_search_create(NULL, 0, NULL);
    } else {
        h = int32_search_create(data, n, NULL);
    }

    int pass = 1;
    size_t idx;

    if (n == 0) {
        if (h != NULL) { printf("  n=0 create should return NULL but didn't\n"); pass = 0; }
        free(data);
        return pass;
    }

    if (h == NULL) { printf("  create failed for n=%zu\n", n); free(data); return 0; }

    int32_t hit_target = data[n / 2];
    int ret = int32_search_find(h, hit_target, &idx);
    if (ret != INT32_SEARCH_OK || idx != n / 2) {
        printf("  n=%zu hit test FAIL: target=%d, ret=%d, idx=%zu\n",
               n, hit_target, ret, idx);
        pass = 0;
    }

    int32_t below_target = data[0] - 1;
    ret = int32_search_find(h, below_target, &idx);
    if (ret != INT32_SEARCH_ERR_NOT_FOUND) {
        printf("  n=%zu below-miss test FAIL: target=%d, ret=%d\n",
               n, below_target, ret);
        pass = 0;
    }

    int32_t above_target = data[n - 1] + 1;
    ret = int32_search_find(h, above_target, &idx);
    if (ret != INT32_SEARCH_ERR_NOT_FOUND) {
        printf("  n=%zu above-miss test FAIL: target=%d, ret=%d\n",
               n, above_target, ret);
        pass = 0;
    }

    int32_search_destroy(h);
    free(data);
    return pass;
}

int main(void)
{
    printf("Boundary Tests (SIMD)\n=====================\n\n");

    int failures = 0;
    for (size_t i = 0; i < num_sizes; i++) {
        size_t n = test_sizes[i];
        printf("n=%-3zu: ", n);
        if (run_boundary_test(n)) {
            printf("PASS\n");
        } else {
            printf("FAIL\n");
            failures++;
        }
    }

    printf("\nTotal: %zu tests, %d failures\n", num_sizes, failures);
    return failures > 0 ? 1 : 0;
}
