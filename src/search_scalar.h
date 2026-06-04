#ifndef INT32_SEARCH_SCALAR_H
#define INT32_SEARCH_SCALAR_H

#include <stdint.h>
#include <stddef.h>

int32_t search_scalar_find(const int32_t *vals, size_t n,
                           int32_t target, size_t *out_index);

size_t search_scalar_lower_bound(const int32_t *vals, size_t n, int32_t target);
size_t search_scalar_upper_bound(const int32_t *vals, size_t n, int32_t target);

#endif
