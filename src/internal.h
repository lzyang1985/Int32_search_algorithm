#ifndef INT32_SEARCH_INTERNAL_H
#define INT32_SEARCH_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include "../include/int32_search.h"

#define PATH_A  0
#define PATH_B1 1

#define INT32_SEARCH_AVX2_MIN_N SIZE_MAX

typedef struct {
    _Atomic(const int32_t  *) vals;
    _Atomic(const uint16_t *) lo16;
    _Atomic(const int32_t  *) dir;
    _Atomic size_t            n;
    int                       path;
    int32_t                  (*search_fn)(const int32_t *vals, size_t n, int32_t key, size_t *out_index);
    uint8_t                   avx2_capable;
    _Atomic size_t            reader_count;
    _Atomic(void *)           bloom;
} int32_search_impl_t;

typedef struct {
    const int32_t  *vals;
    const uint16_t *lo16;
    const int32_t  *dir;
    size_t          n;
} b1_snapshot_t;

#endif
