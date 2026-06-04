#ifndef INT64_SEARCH_H
#define INT64_SEARCH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INT64_SEARCH_OK              0
#define INT64_SEARCH_ERR_NOT_FOUND  -1
#define INT64_SEARCH_ERR_NULL_HANDLE -2
#define INT64_SEARCH_ERR_MEMORY      -3
#define INT64_SEARCH_ERR_INVALID_ARG -4
#define INT64_SEARCH_ERR_TOO_LARGE   -5

typedef void* int64_search_t;

typedef struct {
    int use_bloom;
    int reserved[7];
} int64_search_config_t;

int64_search_t int64_search_create(const int64_t *data, size_t n,
                                    const int64_search_config_t *cfg);
int int64_search_find(int64_search_t handle, int64_t key,
                       size_t *out_index);
int int64_search_destroy(int64_search_t handle);

/* ⚠️ 线程安全警告: int64_search_rebuild 当前不支持并发调用。
 * rebuild 期间若其他线程同时执行 find，存在数据竞争风险。
 * 请确保 rebuild 调用是单线程的，或在外部加锁保护。 */
int int64_search_rebuild(int64_search_t handle,
                          const int64_t *data, size_t n);
const char *int64_search_version(void);
int int64_search_set_bloom_bypass(int64_search_t handle, int bypass);
int int64_search_get_bloom_bypass(int64_search_t handle);

int int64_search_find_range(int64_search_t handle, int64_t low,
                             int64_t high, size_t *out_first,
                             size_t *out_count);

#ifdef __cplusplus
}
#endif
#endif
