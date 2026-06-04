#ifndef INT64_INTERNAL_H
#define INT64_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>

#define PATH_SCALAR 0
#define PATH_B1     1

#define B1_MAX_BUCKET_THRESHOLD_INT64  409
#define B1_FALLBACK_THRESHOLD          409
#define INT64_DIR_ENTRIES              1048576
#define INT64_DIR_SIZE                 (INT64_DIR_ENTRIES + 1)

extern int g_int64_search_debug;

#define INT64_DLOG(fmt, ...) do { \
    if (g_int64_search_debug) \
        fprintf(stderr, "[int64_search] " fmt "\n", ##__VA_ARGS__); \
} while(0)

static inline uint32_t get_bucket_key(int64_t key) {
    return (uint32_t)(((uint64_t)key ^ ((uint64_t)1 << 63)) >> 44);
}

typedef struct {
    int             path;
    size_t          n;
    int64_t        *vals;
    int32_t        *dir;
    _Atomic(void *) bloom;
    _Atomic(int)    bloom_bypass;
} int64_search_impl_t;

#endif
