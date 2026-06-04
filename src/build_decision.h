#ifndef INT32_SEARCH_BUILD_DECISION_H
#define INT32_SEARCH_BUILD_DECISION_H

#include <stdint.h>
#include <stddef.h>

#define B1_MAX_BUCKET_THRESHOLD 2000

int build_decision_select_path(const int32_t *dir, size_t n);

#endif
