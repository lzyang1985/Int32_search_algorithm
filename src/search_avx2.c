#include "search_avx2.h"
#include "internal.h"

#include <immintrin.h>

#include <stdio.h>

#ifdef INT32_SEARCH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define TRACE_LOG(fmt, ...) fprintf(stderr, "[TRACE] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(...)  ((void)0)
#define TRACE_LOG(...)  ((void)0)
#endif

int32_t search_avx2_find(const int32_t *vals, size_t n,
                         int32_t target, size_t *out_index)
{
    if (n == 0) return INT32_SEARCH_ERR_NOT_FOUND;
    if (vals == NULL) return INT32_SEARCH_ERR_INVALID_ARG;

    DEBUG_LOG("search_avx2_find: n=%zu, target=%d", n, target);

    size_t lo = 0, hi = n;

    while (hi - lo >= 8) {
        size_t mid = lo + (hi - lo) / 2;
        size_t block = mid & ~(size_t)7;
        if (block < lo) block = lo;
        if (block + 8 > hi) block = hi - 8; /* 安全: while 条件保证 hi >= 8; 若修改 while 边界(如 >=4)须同步审查此处 */

        __m256i key = _mm256_set1_epi32(target);
        __m256i vec = _mm256_loadu_si256((const __m256i *)(vals + block));
        __m256i cmp = _mm256_cmpgt_epi32(vec, key);
        int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp)); /* Intel 标准惯用法，Haswell+ 无跨域惩罚 */
        int le_count = (int)(8u - (unsigned int)__builtin_popcount((unsigned int)mask));

        TRACE_LOG("block=%zu, lo=%zu, hi=%zu, mask=0x%02x, le_count=%d",
                  block, lo, hi, mask, le_count);

        if (le_count == 0) {
            hi = block;
        } else {
            size_t last_le = block + (size_t)le_count - 1;
            if (vals[last_le] < target) {
                lo = block + (size_t)le_count;
            } else {
                if (block + (size_t)le_count == hi) {
                    break;
                }
                hi = block + (size_t)le_count;
            }
        }
    }

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo < n && vals[lo] == target) {
        if (out_index != NULL) *out_index = lo;
        return INT32_SEARCH_OK;
    }

    return INT32_SEARCH_ERR_NOT_FOUND;
}
