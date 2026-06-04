#include "platform_memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int cmp_int64(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

int64_t *build_sorted_int64(const int64_t *data, size_t n) {
    if (data == NULL || n == 0) return NULL;
    if (n > SIZE_MAX / sizeof(int64_t)) return NULL;

    int64_t *vals = (int64_t *)platform_aligned_alloc(n * sizeof(int64_t));
    if (vals == NULL) return NULL;

    memcpy(vals, data, n * sizeof(int64_t));
    qsort(vals, n, sizeof(int64_t), cmp_int64);

    for (size_t i = 0; i + 1 < n; i++) {
        if (vals[i] > vals[i + 1]) {
            platform_aligned_free(vals);
            return NULL;
        }
    }

    return vals;
}
