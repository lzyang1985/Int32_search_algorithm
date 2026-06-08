#include "../include/int64_search.h"
#include "internal_int64.h"
#include "platform_memory.h"
#include "platform_thread.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef INT64_SEARCH_USE_BLOOM
#include "bloom_filter.h"
#endif

int g_int64_search_debug = 0;

extern int64_t *build_sorted_int64(const int64_t *data, size_t n);
extern int32_t *build_dir_int64(const int64_t *sorted_vals, size_t n);
extern int      build_decision_int64(const int32_t *dir, size_t n);
extern size_t   search_int64_scalar(const int64_t *vals, size_t n, int64_t target);
extern size_t   search_int64_b1(const int64_t *vals, const int32_t *dir,
                                 size_t n, int64_t target);

int64_search_t int64_search_create(const int64_t *data, size_t n,
                                    const int64_search_config_t *cfg)
{
    if (data == NULL || n == 0) return NULL;
    if (n > (size_t)INT32_MAX) return NULL;

    int64_search_impl_t *impl = (int64_search_impl_t *)calloc(1, sizeof(*impl));
    if (impl == NULL) return NULL;

    impl->n = n;

    int64_t *new_vals = build_sorted_int64(data, n);
    if (new_vals == NULL) {
        free(impl);
        return NULL;
    }
    impl->vals = new_vals;

#ifdef INT64_SEARCH_USE_BLOOM
    if (cfg != NULL && cfg->use_bloom) {
        void *new_bloom = bloom_create(n);
        if (new_bloom != NULL) {
            for (size_t i = 0; i < n; i++)
                bloom_insert(new_bloom, new_vals[i]);
            atomic_init(&impl->bloom, new_bloom);
        }
    } else {
        atomic_init(&impl->bloom, NULL);
    }
#else
    (void)cfg;
    atomic_init(&impl->bloom, NULL);
#endif

    int32_t *new_dir = build_dir_int64(new_vals, n);
    int selected_path = build_decision_int64(new_dir, n);

    if (selected_path == PATH_B1) {
        impl->path = PATH_B1;
        impl->dir  = new_dir;
    } else {
        impl->path = PATH_SCALAR;
        if (new_dir != NULL) platform_aligned_free(new_dir);
        impl->dir = NULL;
    }

    atomic_init(&impl->bloom_bypass, 0);

    return (int64_search_t)impl;
}

int int64_search_find(int64_search_t handle, int64_t key,
                       size_t *out_index)
{
    if (handle == NULL) return INT64_SEARCH_ERR_NULL_HANDLE;

    int64_search_impl_t *impl = (int64_search_impl_t *)handle;

    /* === Phase 2 Step 1: 进入 reader 临界区(acquire 语义) === */
    /* fetch_add(acquire) 与 writer 的 exchange(acq_rel) 配对,保证本 reader
     * 后续 load 看到 writer 释放的所有新数据 */
    atomic_fetch_add_explicit(&impl->reader_count, 1, memory_order_acquire);

    /* === Phase 2 Step 2: acquire-load 所有数据快照 === */
    int     p   = atomic_load_explicit(&impl->path, memory_order_acquire);
    size_t  _n  = atomic_load_explicit(&impl->n,    memory_order_acquire);
    const int64_t *v = atomic_load_explicit(&impl->vals, memory_order_acquire);
    const int32_t *d = (p == PATH_B1)
                     ? atomic_load_explicit(&impl->dir, memory_order_acquire)
                     : NULL;

    /* === Step 3: bloom 预筛(保持 Phase 1 行为) === */
    int bypass = atomic_load_explicit(&impl->bloom_bypass, memory_order_relaxed);
#ifdef INT64_SEARCH_USE_BLOOM
    if (!bypass) {
        void *bf = atomic_load_explicit(&impl->bloom, memory_order_acquire);
        if (bf != NULL && !bloom_query(bf, key)) {
            /* 错误路径: 必须先减 reader_count 再 return */
            atomic_fetch_sub_explicit(&impl->reader_count, 1, memory_order_release);
            if (out_index) *out_index = (size_t)-1;
            return INT64_SEARCH_ERR_NOT_FOUND;
        }
    }
#else
    (void)bypass;
#endif

    /* === Step 4: 分派搜索(纯计算,无原子操作) === */
    size_t idx;
    if (p == PATH_B1) {
        idx = search_int64_b1(v, d, _n, key);
    } else {
        idx = search_int64_scalar(v, _n, key);
    }

    /* === Phase 2 Step 5: 退出 reader 临界区(release 语义) === */
    /* release 确保 reader 在临界区内的所有读都对后续 writer 的 acquire 可见 */
    atomic_fetch_sub_explicit(&impl->reader_count, 1, memory_order_release);

    if (idx == (size_t)-1) {
        if (out_index) *out_index = (size_t)-1;
        return INT64_SEARCH_ERR_NOT_FOUND;
    }

    if (out_index) *out_index = idx;
    return INT64_SEARCH_OK;
}

int int64_search_destroy(int64_search_t handle) {
    if (handle == NULL) return INT64_SEARCH_OK;

    int64_search_impl_t *impl = (int64_search_impl_t *)handle;

    /* === Phase 2 Step 1: 等待所有 reader 退出(Q3 决议) === */
    /* 必须先 wait,否则 reader 可能持有已释放的 vals/dir 指针 */
    while (atomic_load_explicit(&impl->reader_count, memory_order_acquire) > 0) {
        platform_thread_yield();
    }

    /* === Step 2: 释放 bloom === */
#ifdef INT64_SEARCH_USE_BLOOM
    void *bf = atomic_load_explicit(&impl->bloom, memory_order_relaxed);
    if (bf != NULL) bloom_destroy(bf);
#endif

    /* === Step 3: 释放 vals/dir === */
    const int64_t *v = atomic_load_explicit(&impl->vals, memory_order_relaxed);
    const int32_t *d = atomic_load_explicit(&impl->dir,  memory_order_relaxed);
    if (v != NULL) platform_aligned_free((void *)v);
    if (d != NULL) platform_aligned_free((void *)d);

    /* === Step 4: 清零 impl + 释放内存(防悬垂指针) === */
    memset(impl, 0, sizeof(*impl));
    free(impl);
    return INT64_SEARCH_OK;
}

int int64_search_rebuild(int64_search_t handle,
                          const int64_t *data, size_t n)
{
    if (handle == NULL) return INT64_SEARCH_ERR_NULL_HANDLE;
    if (data == NULL || n == 0) return INT64_SEARCH_ERR_INVALID_ARG;
    if (n > (size_t)INT32_MAX) return INT64_SEARCH_ERR_TOO_LARGE;

    int64_search_impl_t *impl = (int64_search_impl_t *)handle;

    /* === Phase 2 Phase A: 构造新 vals/dir/path(失败时 impl 保持不动) === */
    int64_t *new_vals = build_sorted_int64(data, n);
    if (new_vals == NULL) return INT64_SEARCH_ERR_MEMORY;

    int32_t *new_dir = build_dir_int64(new_vals, n);
    int new_path = build_decision_int64(new_dir, n);

    /* === Phase 2 Phase B: 构造新 bloom(仅当旧 bloom 存在;失败容忍) === */
    /* DEV-I64-001 修复: rebuild 重建 bloom 保证预筛与新数据一致 */
    void *new_bloom = NULL;
#ifdef INT64_SEARCH_USE_BLOOM
    void *cur_bloom = atomic_load_explicit(&impl->bloom, memory_order_acquire);
    if (cur_bloom != NULL) {
        new_bloom = bloom_create(n);
        if (new_bloom != NULL) {
            for (size_t i = 0; i < n; i++) {
                bloom_insert(new_bloom, new_vals[i]);
            }
        }
        /* bloom_create 失败容忍: new_bloom = NULL,reader 会跳过预筛 */
    }
#endif

    /* === Phase 2 Phase C: 5 字段原子交换(顺序 path → n → dir → vals → bloom) === */
    /* acq_rel exchange: 与 reader 的 fetch_add(acquire) 形成同步关系       */
    /* 必须完整捕获每个字段的旧值,旧值在 Phase E 统一释放(否则内存泄漏) */
    (void)atomic_exchange_explicit(&impl->path, new_path, memory_order_acq_rel);
    (void)atomic_exchange_explicit(&impl->n,    n,        memory_order_acq_rel);
    const int32_t *old_dir = atomic_exchange_explicit(
        &impl->dir, (new_path == PATH_B1) ? new_dir : NULL, memory_order_acq_rel);
    const int64_t *old_vals = atomic_exchange_explicit(
        &impl->vals, new_vals, memory_order_acq_rel);
#ifdef INT64_SEARCH_USE_BLOOM
    void *old_bloom = atomic_exchange_explicit(
        &impl->bloom, new_bloom, memory_order_acq_rel);
#else
    (void)atomic_exchange_explicit(&impl->bloom, new_bloom, memory_order_acq_rel);
#endif

    /* === Phase 2 Phase D: 等待所有 reader 退出 === */
    /* 交换后,旧 vals/dir 仍可能正在被 reader 持有快照引用 */
    while (atomic_load_explicit(&impl->reader_count, memory_order_acquire) > 0) {
        platform_thread_yield();
    }

    /* === Phase 2 Phase E: 释放旧数据(此时已无 reader 持有旧指针) === */
    if (old_vals != NULL) platform_aligned_free((void *)old_vals);
    if (old_dir  != NULL) platform_aligned_free((void *)old_dir);
#ifdef INT64_SEARCH_USE_BLOOM
    if (old_bloom != NULL) bloom_destroy(old_bloom);
#endif
    /* new_dir 的释放:仅在 PATH_SCALAR 时需要(PATH_B1 时已通过 dir 字段交换给 reader) */
    if (new_path != PATH_B1 && new_dir != NULL) {
        platform_aligned_free(new_dir);
    }

    return INT64_SEARCH_OK;
}

const char *int64_search_version(void) {
    return "libint64search 0.2.0";
}

int int64_search_set_bloom_bypass(int64_search_t handle, int bypass) {
    if (handle == NULL) return INT64_SEARCH_ERR_NULL_HANDLE;

    int64_search_impl_t *impl = (int64_search_impl_t *)handle;
    atomic_store_explicit(&impl->bloom_bypass, bypass ? 1 : 0, memory_order_relaxed);

    return INT64_SEARCH_OK;
}

int int64_search_get_bloom_bypass(int64_search_t handle) {
    if (handle == NULL) return INT64_SEARCH_ERR_NULL_HANDLE;

    int64_search_impl_t *impl = (int64_search_impl_t *)handle;
    return atomic_load_explicit(&impl->bloom_bypass, memory_order_relaxed);
}

int int64_search_find_range(int64_search_t handle, int64_t low,
                             int64_t high, size_t *out_first,
                             size_t *out_count)
{
    (void)handle; (void)low; (void)high; (void)out_first; (void)out_count;
    return INT64_SEARCH_ERR_NOT_FOUND;
}
