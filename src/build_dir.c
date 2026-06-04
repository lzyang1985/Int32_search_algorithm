#include "build_dir.h"

#include <stdlib.h>
#include <stdio.h>

#ifdef INT32_SEARCH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(...)  ((void)0)
#endif

int32_t *build_dir(const int32_t *vals, size_t n)
{
    if (vals == NULL || n == 0) return NULL;
    if (n > (size_t)INT32_MAX) return NULL;

    int32_t *dir = (int32_t *)malloc(INT32_SEARCH_DIR_SIZE * sizeof(int32_t));
    if (dir == NULL) return NULL;

    for (int i = 0; i < INT32_SEARCH_DIR_SIZE; i++) {
        dir[i] = -1;
    }

    for (size_t i = 0; i < n; i++) {
        uint16_t h = (uint16_t)((uint32_t)vals[i] >> 16) ^ 0x8000;
        if (dir[h] == -1) {
            dir[h] = (int32_t)i;
        }
    }

    int32_t next_start = (int32_t)n;
    for (int i = 65535; i >= 0; i--) {
        if (dir[i] == -1) {
            dir[i] = next_start;
        } else {
            next_start = dir[i];
        }
    }
    dir[65536] = (int32_t)n;

    DEBUG_LOG("build_dir: dir built, n=%zu", n);
    return dir;
}

int dir_validate(const int32_t *dir, size_t n)
{
    if (dir == NULL) return 0;

    for (int i = 0; i < 65536; i++) {
        if (dir[i] < 0 || dir[i] > (int32_t)n) return 0;
        if (dir[i] > dir[i + 1]) return 0;
    }

    if (dir[65536] != (int32_t)n) return 0;

    return 1;
}
