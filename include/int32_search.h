#ifndef INT32_SEARCH_H
#define INT32_SEARCH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INT32_SEARCH_OK              0
#define INT32_SEARCH_ERR_NOT_FOUND  -1
#define INT32_SEARCH_ERR_NULL_HANDLE -2
#define INT32_SEARCH_ERR_MEMORY      -3
#define INT32_SEARCH_ERR_INVALID_ARG -4

typedef void* int32_search_t;

typedef struct {
    int use_bloom;
    int reserved[7];
} int32_search_config_t;

int32_search_t int32_search_create(const int32_t *data, size_t n,
                                    const int32_search_config_t *cfg);
int int32_search_find(int32_search_t handle, int32_t key,
                       size_t *out_index);
int int32_search_destroy(int32_search_t handle);
int int32_search_rebuild(int32_search_t handle,
                          const int32_t *data, size_t n);
int int32_search_find_range(int32_search_t handle, int32_t low,
                             int32_t high, size_t *out_first,
                             size_t *out_count);
const char *int32_search_version(void);

#ifdef __cplusplus
}
#endif
#endif
