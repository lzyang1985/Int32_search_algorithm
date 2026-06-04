#include "build_decision.h"
#include "internal.h"

#include <stdio.h>

#ifdef INT32_SEARCH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(...)  ((void)0)
#endif

int build_decision_select_path(const int32_t *dir, size_t n)
{
    if (dir == NULL || n == 0) return PATH_A;

    size_t max_sz = 0;
    for (int i = 0; i < 65536; i++) {
        int32_t sz = dir[i + 1] - dir[i];
        if (sz > 0 && (size_t)sz > max_sz) max_sz = (size_t)sz;
    }

    if (max_sz > n / 10) {
        DEBUG_LOG("build_decision: max_bucket=%zu > n/10=%zu → PATH_A (skewed)",
                  max_sz, n / 10);
        return PATH_A;
    }

    if (max_sz <= B1_MAX_BUCKET_THRESHOLD) {
        DEBUG_LOG("build_decision: max_bucket=%zu <= %d → PATH_B1",
                  max_sz, B1_MAX_BUCKET_THRESHOLD);
        return PATH_B1;
    }

    DEBUG_LOG("build_decision: max_bucket=%zu > %d → PATH_A",
              max_sz, B1_MAX_BUCKET_THRESHOLD);
    return PATH_A;
}
