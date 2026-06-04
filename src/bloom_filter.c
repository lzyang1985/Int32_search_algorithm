#include "bloom_filter.h"

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash/xxhash.h"

#include <stdlib.h>
#include <string.h>

bloom_filter_t *bloom_create(size_t n)
{
    bloom_filter_t *bf = (bloom_filter_t *)calloc(1, sizeof(*bf));
    if (bf == NULL) return NULL;

    bf->m = n * BLOOM_BITS_PER;
    if (bf->m == 0) bf->m = BLOOM_BITS_PER;

    size_t byte_sz = (bf->m + 7) / 8;
    bf->bits = (uint8_t *)calloc(1, byte_sz);
    if (bf->bits == NULL) {
        free(bf);
        return NULL;
    }

    bf->seeds[0] = 0x9747b28c;
    bf->seeds[1] = 0x5d3a11e7;
    bf->seeds[2] = 0x1f8c3e96;

    return bf;
}

void bloom_insert(bloom_filter_t *bf, int32_t key)
{
    int i;
    for (i = 0; i < BLOOM_K; i++) {
        uint64_t h = (uint64_t)XXH32(&key, sizeof(key), bf->seeds[i]);
        size_t bit = (size_t)(h % bf->m);
        bf->bits[bit / 8] |= (uint8_t)(1U << (bit % 8));
    }
}

int bloom_query(const bloom_filter_t *bf, int32_t key)
{
    int i;
    for (i = 0; i < BLOOM_K; i++) {
        uint64_t h = (uint64_t)XXH32(&key, sizeof(key), bf->seeds[i]);
        size_t bit = (size_t)(h % bf->m);
        if (!(bf->bits[bit / 8] & (uint8_t)(1U << (bit % 8)))) {
            return 0;
        }
    }
    return 1;
}

void bloom_destroy(bloom_filter_t *bf)
{
    if (bf == NULL) return;
    free(bf->bits);
    free(bf);
}
