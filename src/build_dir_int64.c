#include "internal_int64.h"
#include "platform_memory.h"
#include <stdint.h>
#include <stddef.h>

int32_t *build_dir_int64(const int64_t *sorted_vals, size_t n) {
    if (n > (size_t)INT32_MAX) return NULL;

    int32_t *dir = (int32_t *)platform_aligned_alloc(INT64_DIR_SIZE * sizeof(int32_t));
    if (dir == NULL) return NULL;

    for (int i = 0; i < INT64_DIR_SIZE; i++)
        dir[i] = -1;

    for (size_t j = 0; j < n; j++) {
        uint32_t h = get_bucket_key(sorted_vals[j]);
        if (dir[h] == -1)
            dir[h] = (int32_t)j;
    }

    dir[INT64_DIR_ENTRIES] = (int32_t)n;

    for (int i = INT64_DIR_ENTRIES - 1; i >= 0; i--) {
        if (dir[i] == -1)
            dir[i] = dir[i + 1];
    }

    return dir;
}

int dir_validate_int64(const int32_t *dir, size_t n) {
    if (dir == NULL) return 0;

    for (int i = 0; i < INT64_DIR_ENTRIES; i++) {
        if (dir[i] > dir[i + 1]) {
            INT64_DLOG("dir_validate FAILED: i=%d, dir[i]=%d, dir[i+1]=%d",
                       i, dir[i], dir[i + 1]);
            return 0;
        }
    }

    if (dir[INT64_DIR_ENTRIES] != (int32_t)n) {
        INT64_DLOG("dir_validate FAILED: sentinel=%d != n=%zu",
                   dir[INT64_DIR_ENTRIES], n);
        return 0;
    }

    return 1;
}
