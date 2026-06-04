#ifndef INT32_SEARCH_BUILD_DIR_H
#define INT32_SEARCH_BUILD_DIR_H

#include <stdint.h>
#include <stddef.h>

#define INT32_SEARCH_DIR_SIZE 65537

int32_t *build_dir(const int32_t *vals, size_t n);

int dir_validate(const int32_t *dir, size_t n);

#endif
