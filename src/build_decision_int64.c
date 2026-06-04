#include "internal_int64.h"

int build_decision_int64(const int32_t *dir, size_t n) {
    if (dir == NULL)
        return PATH_SCALAR;

    for (int i = 0; i < INT64_DIR_ENTRIES; i++) {
        if (dir[i] > dir[i + 1]) {
            INT64_DLOG("build_decision: dir[%d]=%d > dir[%d]=%d -> PATH_SCALAR",
                       i, dir[i], i + 1, dir[i + 1]);
            return PATH_SCALAR;
        }
    }

    if (dir[INT64_DIR_ENTRIES] != (int32_t)n) {
        INT64_DLOG("build_decision: sentinel mismatch -> PATH_SCALAR");
        return PATH_SCALAR;
    }

    int32_t max_bucket = 0;
    for (int i = 0; i < INT64_DIR_ENTRIES; i++) {
        int32_t sz = dir[i + 1] - dir[i];
        if (sz > max_bucket) max_bucket = sz;
    }

    INT64_DLOG("build_decision: max_bucket=%d, threshold=%d -> %s",
               max_bucket, B1_MAX_BUCKET_THRESHOLD_INT64,
               max_bucket > B1_MAX_BUCKET_THRESHOLD_INT64 ? "PATH_SCALAR" : "PATH_B1");

    if (max_bucket > B1_MAX_BUCKET_THRESHOLD_INT64)
        return PATH_SCALAR;

    return PATH_B1;
}
