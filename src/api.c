#include "../include/int32_search.h"
#include "internal.h"
#include "platform_memory.h"
#include "platform_cpu.h"
#include "platform_thread.h"
#include "build_sorted.h"
#include "build_dir.h"
#include "build_b1.h"
#include "build_decision.h"
#include "search_scalar.h"
#include "search_avx2.h"
#include "search_b1.h"

#ifdef INT32_SEARCH_USE_BLOOM
#include "bloom_filter.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef INT32_SEARCH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define ERROR_LOG(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(...)  ((void)0)
#define ERROR_LOG(...)  ((void)0)
#endif

int32_search_t int32_search_create(const int32_t *data, size_t n,
                                    const int32_search_config_t *cfg)
{
    if (data == NULL || n == 0) return NULL;

    (void)cfg;
    DEBUG_LOG("int32_search_create: n=%zu", n);

    int32_search_impl_t *impl = (int32_search_impl_t *)calloc(1, sizeof(*impl));
    if (impl == NULL) {
        ERROR_LOG("int32_search_create: malloc impl failed");
        return NULL;
    }

    int32_t *new_vals = build_sort_and_validate(data, n);
    if (new_vals == NULL) {
        ERROR_LOG("int32_search_create: build_sort_and_validate failed");
        free(impl);
        return NULL;
    }

#ifdef INT32_SEARCH_USE_BLOOM
    bloom_filter_t *new_bloom = NULL;
    if (cfg != NULL && cfg->use_bloom) {
        new_bloom = bloom_create(n);
        if (new_bloom == NULL) {
            ERROR_LOG("int32_search_create: bloom_create failed → continue without bloom");
        } else {
            size_t i;
            for (i = 0; i < n; i++) {
                bloom_insert(new_bloom, new_vals[i]);
            }
            DEBUG_LOG("int32_search_create: bloom enabled, m=%zu", new_bloom->m);
        }
    }
#endif

    int32_t *new_dir = build_dir(new_vals, n);
    if (new_dir == NULL) {
        ERROR_LOG("int32_search_create: build_dir failed → fallback PATH_A");
        goto setup_path_a;
    }

    if (!dir_validate(new_dir, n)) {
        ERROR_LOG("int32_search_create: dir_validate failed → fallback PATH_A");
        free(new_dir);
        goto setup_path_a;
    }

    uint16_t *new_lo16 = build_b1(new_vals, n, new_dir);
    if (new_lo16 == NULL) {
        ERROR_LOG("int32_search_create: build_b1 failed → fallback PATH_A");
        free(new_dir);
        goto setup_path_a;
    }

    int selected_path = build_decision_select_path(new_dir, n);
    DEBUG_LOG("int32_search_create: selected_path=%s",
              selected_path == PATH_B1 ? "B1" : "A");

    if (selected_path == PATH_B1) {
        impl->path = PATH_B1;
        atomic_init(&impl->vals, new_vals);
        atomic_init(&impl->lo16, new_lo16);
        atomic_init(&impl->dir,  new_dir);
        goto setup_common;
    }

    free(new_dir);
    free(new_lo16);

setup_path_a:
    impl->path = PATH_A;
    atomic_init(&impl->vals, new_vals);
    atomic_init(&impl->lo16, NULL);
    atomic_init(&impl->dir,  NULL);

setup_common:
    impl->n = n;
    impl->avx2_capable = (uint8_t)platform_cpu_has_avx2();
#ifdef INT32SEARCH_FORCE_AVX2
    if (impl->avx2_capable) {
        impl->search_fn = search_avx2_find;
    } else {
        impl->search_fn = search_scalar_find;
    }
#else
    if (impl->avx2_capable && impl->n > INT32_SEARCH_AVX2_MIN_N) {
        impl->search_fn = search_avx2_find;
    } else {
        impl->search_fn = search_scalar_find;
    }
#endif
    atomic_init(&impl->reader_count, 0);
#ifdef INT32_SEARCH_USE_BLOOM
    atomic_init(&impl->bloom, new_bloom);
#else
    atomic_init(&impl->bloom, NULL);
#endif

    DEBUG_LOG("int32_search_create: path=%s, search_fn=%s",
              impl->path == PATH_B1 ? "B1" : "A",
              impl->search_fn == search_avx2_find ? "avx2" : "scalar");

    return (int32_search_t)impl;
}

int int32_search_find(int32_search_t handle, int32_t key,
                      size_t *out_index)
{
    if (handle == NULL) return INT32_SEARCH_ERR_NULL_HANDLE;
    if (out_index == NULL) return INT32_SEARCH_ERR_INVALID_ARG;

    int32_search_impl_t *impl = (int32_search_impl_t *)handle;

    DEBUG_LOG("int32_search_find: key=%d", key);

    atomic_size_fetch_add(&impl->reader_count, 1, memory_order_acquire);

#ifdef INT32_SEARCH_USE_BLOOM
    {
        const bloom_filter_t *bf = (const bloom_filter_t *)atomic_ptr_load(&impl->bloom, memory_order_acquire);
        if (bf != NULL && !bloom_query(bf, key)) {
            DEBUG_LOG("int32_search_find: bloom rejected key=%d", key);
            atomic_size_fetch_sub(&impl->reader_count, 1, memory_order_release);
            return INT32_SEARCH_ERR_NOT_FOUND;
        }
    }
#endif

    int32_t result;
    if (impl->path == PATH_B1) {
        const int32_t  *v = atomic_ptr_load(&impl->vals, memory_order_acquire);
        const uint16_t *l = atomic_ptr_load(&impl->lo16, memory_order_acquire);
        const int32_t  *d = atomic_ptr_load(&impl->dir,  memory_order_acquire);
        size_t _n = atomic_size_load(&impl->n, memory_order_acquire);
        result = search_b1_find(v, l, d, _n, key, out_index);
    } else {
        const int32_t *v = atomic_ptr_load(&impl->vals, memory_order_acquire);
        size_t _n = atomic_size_load(&impl->n, memory_order_acquire);
        result = impl->search_fn(v, _n, key, out_index);
    }

    atomic_size_fetch_sub(&impl->reader_count, 1, memory_order_release);

    return result;
}

int int32_search_rebuild(int32_search_t handle,
                          const int32_t *data, size_t n)
{
    if (handle == NULL) return INT32_SEARCH_ERR_NULL_HANDLE;
    if (data == NULL || n == 0) return INT32_SEARCH_ERR_INVALID_ARG;

    int32_search_impl_t *impl = (int32_search_impl_t *)handle;

    DEBUG_LOG("int32_search_rebuild: n=%zu", n);

    int32_t *new_vals = build_sort_and_validate(data, n);
    if (new_vals == NULL) {
        ERROR_LOG("int32_search_rebuild: build_sort_and_validate failed");
        return INT32_SEARCH_ERR_MEMORY;
    }

    int32_t  *new_dir = build_dir(new_vals, n);
    int       new_path;
    uint16_t *new_lo16 = NULL;

    if (new_dir == NULL || !dir_validate(new_dir, n)) {
        new_path = PATH_A;
        free(new_dir);
        new_dir = NULL;
    } else {
        new_lo16 = build_b1(new_vals, n, new_dir);
        if (new_lo16 == NULL) {
            new_path = PATH_A;
            free(new_dir);
            new_dir = NULL;
        } else {
            new_path = build_decision_select_path(new_dir, n);
            if (new_path == PATH_A) {
                free(new_dir);
                free(new_lo16);
                new_dir = NULL;
                new_lo16 = NULL;
            }
        }
    }

#ifdef INT32_SEARCH_USE_BLOOM
    bloom_filter_t *new_bloom = NULL;
    {
        const bloom_filter_t *old_bf = (const bloom_filter_t *)atomic_ptr_load(&impl->bloom, memory_order_relaxed);
        if (old_bf != NULL) {
            new_bloom = bloom_create(n);
            if (new_bloom != NULL) {
                size_t bi;
                for (bi = 0; bi < n; bi++) {
                    bloom_insert(new_bloom, new_vals[bi]);
                }
            }
        }
    }
#endif

    atomic_size_store(&impl->n, n, memory_order_release);

    const int32_t  *old_vals;
    const uint16_t *old_lo16;
    const int32_t  *old_dir;

    if (new_path == PATH_B1) {
        old_lo16 = atomic_ptr_exchange(&impl->lo16, new_lo16, memory_order_acq_rel);
        old_dir  = atomic_ptr_exchange(&impl->dir,  new_dir,  memory_order_acq_rel);
        old_vals = atomic_ptr_exchange(&impl->vals, new_vals, memory_order_acq_rel);
    } else {
        old_lo16 = atomic_ptr_exchange(&impl->lo16, NULL, memory_order_acq_rel);
        old_dir  = atomic_ptr_exchange(&impl->dir,  NULL, memory_order_acq_rel);
        old_vals = atomic_ptr_exchange(&impl->vals, new_vals, memory_order_acq_rel);
    }

    impl->path = new_path;

#ifdef INT32_SEARCH_USE_BLOOM
    bloom_filter_t *old_bloom_ex = NULL;
    {
        old_bloom_ex = (bloom_filter_t *)atomic_ptr_exchange(&impl->bloom, new_bloom, memory_order_acq_rel);
    }
#endif

    DEBUG_LOG("int32_search_rebuild: new_path=%s, swapped",
              new_path == PATH_B1 ? "B1" : "A");

    while (atomic_size_load(&impl->reader_count, memory_order_acquire) > 0) {
        platform_thread_yield();
    }

    if (old_vals != NULL) platform_aligned_free((void *)old_vals);
    if (old_lo16 != NULL) free((void *)old_lo16);
    if (old_dir  != NULL) free((void *)old_dir);

#ifdef INT32_SEARCH_USE_BLOOM
    if (old_bloom_ex != NULL) bloom_destroy(old_bloom_ex);
#endif

    DEBUG_LOG("int32_search_rebuild: old data freed, done");
    return INT32_SEARCH_OK;
}

int int32_search_destroy(int32_search_t handle)
{
    if (handle == NULL) return INT32_SEARCH_OK;

    int32_search_impl_t *impl = (int32_search_impl_t *)handle;

    DEBUG_LOG("int32_search_destroy: n=%zu",
              atomic_size_load(&impl->n, memory_order_relaxed));

    while (atomic_size_load(&impl->reader_count, memory_order_acquire) > 0) {
        platform_thread_yield();
    }

    const int32_t *v = atomic_ptr_load(&impl->vals, memory_order_relaxed);
    if (v != NULL) {
        platform_aligned_free((void *)v);
    }

    if (impl->path == PATH_B1) {
        const uint16_t *l = atomic_ptr_load(&impl->lo16, memory_order_relaxed);
        const int32_t  *d = atomic_ptr_load(&impl->dir,  memory_order_relaxed);
        if (l != NULL) free((void *)l);
        if (d != NULL) free((void *)d);
    }

#ifdef INT32_SEARCH_USE_BLOOM
    {
        bloom_filter_t *bf = (bloom_filter_t *)atomic_ptr_load(&impl->bloom, memory_order_relaxed);
        if (bf != NULL) bloom_destroy(bf);
    }
#endif

    memset(impl, 0, sizeof(*impl));
    free(impl);

    return INT32_SEARCH_OK;
}

const char *int32_search_version(void)
{
    return "libint32search 1.0.0";
}

int int32_search_find_range(int32_search_t handle, int32_t low,
                             int32_t high, size_t *out_first,
                             size_t *out_count)
{
    if (handle == NULL) return INT32_SEARCH_ERR_NULL_HANDLE;
    if (out_first == NULL || out_count == NULL) return INT32_SEARCH_ERR_INVALID_ARG;
    if (low > high) return INT32_SEARCH_ERR_INVALID_ARG;

    int32_search_impl_t *impl = (int32_search_impl_t *)handle;

    DEBUG_LOG("int32_search_find_range: low=%d high=%d", low, high);

    atomic_size_fetch_add(&impl->reader_count, 1, memory_order_acquire);

    const int32_t *v = atomic_ptr_load(&impl->vals, memory_order_acquire);
    size_t _n = atomic_size_load(&impl->n, memory_order_acquire);

    size_t first = search_scalar_lower_bound(v, _n, low);
    size_t last  = search_scalar_upper_bound(v, _n, high);

    *out_first = first;
    *out_count = last - first;

    DEBUG_LOG("int32_search_find_range: first=%zu count=%zu", first, *out_count);

    atomic_size_fetch_sub(&impl->reader_count, 1, memory_order_release);

    if (*out_count == 0) return INT32_SEARCH_ERR_NOT_FOUND;
    return INT32_SEARCH_OK;
}
