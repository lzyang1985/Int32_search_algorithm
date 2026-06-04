#include "build_b1.h"

#include <stdlib.h>
#include <stdio.h>

#ifdef INT32_SEARCH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(...)  ((void)0)
#endif

uint16_t *build_b1(const int32_t *vals, size_t n, const int32_t *dir)
{
    (void)dir;
    if (vals == NULL || n == 0) return NULL;
    if (n > SIZE_MAX / sizeof(uint16_t)) return NULL;

    uint16_t *lo16 = (uint16_t *)malloc(n * sizeof(uint16_t));
    if (lo16 == NULL) return NULL;

    for (size_t i = 0; i < n; i++) {
        lo16[i] = (uint16_t)(vals[i] & 0xFFFF);
    }

    DEBUG_LOG("build_b1: lo16 built, n=%zu", n);
    return lo16;
}
