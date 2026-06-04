#ifndef INT32_SEARCH_AVX2_H
#define INT32_SEARCH_AVX2_H

#include <stdint.h>
#include <stddef.h>

int32_t search_avx2_find(const int32_t *vals, size_t n,
                         int32_t target, size_t *out_index);

#endif
