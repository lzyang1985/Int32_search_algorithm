#include "build_sorted.h"
#include "platform_memory.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#ifdef INT32_SEARCH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define ERROR_LOG(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(...)  ((void)0)
#define ERROR_LOG(...)  ((void)0)
#endif

static int compare_int32(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

int32_t *build_sort_and_validate(const int32_t *data, size_t n)
{
    if (data == NULL || n == 0) return NULL;
    if (n > SIZE_MAX / sizeof(int32_t)) return NULL;

    DEBUG_LOG("build_sort_and_validate: n=%zu, first=%d, last=%d",
              n, data[0], data[n - 1]);

    int32_t *vals = (int32_t *)platform_aligned_alloc(n * sizeof(int32_t));
    if (vals == NULL) {
        ERROR_LOG("build_sort_and_validate: malloc failed, size=%zu",
                  n * sizeof(int32_t));
        return NULL;
    }

    memcpy(vals, data, n * sizeof(int32_t));
    qsort(vals, n, sizeof(int32_t), compare_int32);

    for (size_t i = 0; i + 1 < n; i++) {
        if (vals[i] > vals[i + 1]) {
            ERROR_LOG("build_sort_and_validate: monotonicity check failed at i=%zu", i);
            platform_aligned_free(vals);
            return NULL;
        }
    }

    return vals;
}
