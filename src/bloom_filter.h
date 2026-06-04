#ifndef INT32_SEARCH_BLOOM_FILTER_H
#define INT32_SEARCH_BLOOM_FILTER_H

#include <stdint.h>
#include <stddef.h>

#define BLOOM_K         3
#define BLOOM_BITS_PER  12

typedef struct {
    uint8_t *bits;
    size_t   m;
    uint32_t seeds[BLOOM_K];
} bloom_filter_t;

bloom_filter_t *bloom_create(size_t n);
void            bloom_insert(bloom_filter_t *bf, int32_t key);
int             bloom_query(const bloom_filter_t *bf, int32_t key);
void            bloom_destroy(bloom_filter_t *bf);

#endif
