#ifndef INT32_SEARCH_SEARCH_B1_H
#define INT32_SEARCH_SEARCH_B1_H

#include <stdint.h>
#include <stddef.h>

int32_t search_b1_find(const int32_t *vals, const uint16_t *lo16,
                       const int32_t *dir, size_t n, int32_t target,
                       size_t *out_index);

#endif
