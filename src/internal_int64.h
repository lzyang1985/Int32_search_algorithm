#ifndef INT64_INTERNAL_H
#define INT64_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>

#define PATH_SCALAR 0
#define PATH_B1     1

#define B1_MAX_BUCKET_THRESHOLD_INT64  409
#define B1_FALLBACK_THRESHOLD          409
#define INT64_DIR_ENTRIES              1048576
#define INT64_DIR_SIZE                 (INT64_DIR_ENTRIES + 1)

extern int g_int64_search_debug;

#define INT64_DLOG(fmt, ...) do { \
    if (g_int64_search_debug) \
        fprintf(stderr, "[int64_search] " fmt "\n", ##__VA_ARGS__); \
} while(0)

static inline uint32_t get_bucket_key(int64_t key) {
    return (uint32_t)(((uint64_t)key ^ ((uint64_t)1 << 63)) >> 44);
}

/* ============================================================================
 * internal_int64.h — Int64 B1 内部结构 (Phase 2: COW + Bloom 重建)
 * ----------------------------------------------------------------------------
 * Phase 2 变更 (task_006_int64_phase2_cow, ACT-38):
 *   - path/n/vals/dir 改为 _Atomic (TASK T1)
 *   - 新增 reader_count _Atomic size_t (COW 同步原语)
 *   - 添加编译时 lock-free 静态断言 (Phase 2 安全门禁)
 *
 * 内存序约定 (见 DESIGN §4.2):
 *   - reader: fetch_add(reader_count, +1, acquire) → load(acquire) → fetch_sub(-1, release)
 *   - writer: exchange(..., acq_rel) → 等 reader_count==0 → free old
 * ============================================================================ */

#ifdef INT64_SEARCH_MULTI_THREAD
/* ── 多线程 COW 模式: 原子字段 + reader_count ── */
typedef struct {
    _Atomic(void *) bloom;        /* 8 字节,lock-free */
    _Atomic(int)    bloom_bypass; /* 4 字节,lock-free */

    _Atomic int              path;  /* PATH_SCALAR / PATH_B1; 4 字节 lock-free */
    _Atomic size_t           n;     /* 数据规模; 8 字节 lock-free */
    _Atomic(const int64_t *) vals;  /* 排序后的 int64_t 数组; 8 字节 lock-free */
    _Atomic(const int32_t *) dir;   /* B1 high20 目录; 8 字节 lock-free */

    _Atomic size_t reader_count;    /* 进入/退出 reader 临界区计数; 8 字节 lock-free */
} int64_search_impl_t;
#else
/* ── 单线程模式 (D-156 决议): 裸字段,零原子开销 ── */
typedef struct {
    void          *bloom;
    int            bloom_bypass;

    int            path;
    size_t         n;
    const int64_t *vals;
    const int32_t *dir;
} int64_search_impl_t;
#endif

/* ----------------------------------------------------------------------------
 * 编译时 lock-free 验证 (Phase 2 INV-8, 仅多线程模式)
 * 失败时拒绝编译,避免运行期退化为 mutex(性能崩溃)
 * -------------------------------------------------------------------------- */
#ifdef INT64_SEARCH_MULTI_THREAD
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
  /* C11 stdatomic.h 提供的 lock-free 常量 */
  _Static_assert(ATOMIC_INT_LOCK_FREE == 2,
                 "int64_search_impl_t::path requires lock-free _Atomic int");
  _Static_assert(ATOMIC_POINTER_LOCK_FREE == 2,
                 "int64_search_impl_t::bloom/vals/dir require lock-free _Atomic pointer");
#endif

#if defined(__GNUC__) || defined(__clang__)
  /* GCC/Clang 扩展: 在编译时精确判断 size_t 是否 lock-free */
  _Static_assert(__atomic_always_lock_free(sizeof(size_t), 0),
                 "int64_search_impl_t::n/reader_count requires lock-free _Atomic size_t");
#endif
#endif

#endif
