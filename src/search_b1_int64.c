#include "internal_int64.h"
#include <stdint.h>
#include <stddef.h>
#include <immintrin.h>

size_t search_int64_scalar(const int64_t *vals, size_t n, int64_t target);

size_t search_int64_b1(const int64_t *vals, const int32_t *dir,
                        size_t n, int64_t target)
{
    if (n == 0 || vals == NULL || dir == NULL) return (size_t)-1;

    uint32_t h = get_bucket_key(target);
    int32_t start = dir[h];
    int32_t end   = dir[h + 1];

    /* D-143: corrupted dir defense-in-depth.
     * start<0 catches negative indices; start>=end catches inverted range;
     * (size_t)end > n catches out-of-bounds upper bound. */
    if (start < 0 || start >= end) return (size_t)-1;
    if ((size_t)end > n) return (size_t)-1;

    int32_t bucket_sz = end - start;

    if (bucket_sz > B1_FALLBACK_THRESHOLD)
        return search_int64_scalar(vals + start, bucket_sz, target);

#ifdef INT64_SEARCH_B1_SMALL_BUCKET
    /* D-142: small-bucket scalar fast path — 跳过 SIMD 固定开销.
     * 阈值=4: 4个元素时 SIMD (broadcast + cmpeq + movemask ≈ 7cy) 与标量持平.
     * 默认关闭, PGO/LTO 后重评估是否默认开启. */
    if (end - start < 4) {
        for (int32_t i = start; i < end; i++) {
            if (vals[i] == target)
                return (size_t)i;
        }
        return (size_t)-1;
    }
#endif

    __m256i key4 = _mm256_set1_epi64x(target);
    int32_t i = start;
    for (; i + 4 <= end; i += 4) {
        __m256i vec4 = _mm256_loadu_si256((const __m256i *)(vals + i));
        __m256i eq   = _mm256_cmpeq_epi64(key4, vec4);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(eq));
        if (mask != 0) {
            int idx = i + __builtin_ctz(mask);
            if (vals[idx] == target)
                return (size_t)idx;
        }
    }

    for (; i < end; i++) {
        if (vals[i] == target)
            return (size_t)i;
    }

    return (size_t)-1;
}
