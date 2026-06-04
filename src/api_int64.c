#include "../include/int64_search.h"
#include "internal_int64.h"
#include "platform_memory.h"

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

    int bypass = atomic_load_explicit(&impl->bloom_bypass, memory_order_relaxed);
#ifdef INT64_SEARCH_USE_BLOOM
    if (!bypass) {
        void *bf = atomic_load_explicit(&impl->bloom, memory_order_acquire);
        if (bf != NULL && !bloom_query(bf, key)) {
            if (out_index) *out_index = (size_t)-1;
            return INT64_SEARCH_ERR_NOT_FOUND;
        }
    }
#else
    (void)bypass;
#endif

    size_t idx;
    if (impl->path == PATH_B1)
        idx = search_int64_b1(impl->vals, impl->dir, impl->n, key);
    else
        idx = search_int64_scalar(impl->vals, impl->n, key);

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

#ifdef INT64_SEARCH_USE_BLOOM
    void *bf = atomic_load_explicit(&impl->bloom, memory_order_relaxed);
    if (bf != NULL) bloom_destroy(bf);
#endif

    if (impl->vals != NULL) platform_aligned_free(impl->vals);
    if (impl->dir  != NULL) platform_aligned_free(impl->dir);
    impl->vals = NULL;
    impl->dir  = NULL;

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

    int64_t *new_vals = build_sorted_int64(data, n);
    if (new_vals == NULL) return INT64_SEARCH_ERR_MEMORY;

#ifdef INT64_SEARCH_USE_BLOOM
    void *old_bloom = atomic_load_explicit(&impl->bloom, memory_order_relaxed);
    if (old_bloom != NULL) {
        bloom_destroy(old_bloom);
        atomic_store_explicit(&impl->bloom, NULL, memory_order_release);
    }
#endif

    int32_t *new_dir = build_dir_int64(new_vals, n);
    int new_path = build_decision_int64(new_dir, n);

    if (impl->vals != NULL) platform_aligned_free(impl->vals);
    if (impl->dir  != NULL) platform_aligned_free(impl->dir);

    impl->vals = new_vals;
    impl->n    = n;

    if (new_path == PATH_B1) {
        impl->path = PATH_B1;
        impl->dir  = new_dir;
    } else {
        impl->path = PATH_SCALAR;
        if (new_dir != NULL) platform_aligned_free(new_dir);
        impl->dir = NULL;
    }

    return INT64_SEARCH_OK;
}

const char *int64_search_version(void) {
    return "libint64search 0.1.0";
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
