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

/* 线程模型(Phase 2):
 *   - 多 reader 并发调用 int64_search_find 线程安全(lock-free COW 读快照)
 *   - int64_search_rebuild 仍需由单线程调用(COW 写者)
 *     rebuild 期间 reader 可继续调用 find(读到旧或新快照,不会出现撕裂状态)
 *   - int64_search_destroy 等待所有 reader 退出后才释放底层数据(Q3 决议)
 *   - int64_search_set_bloom_bypass 与多 reader 并发安全 */
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
