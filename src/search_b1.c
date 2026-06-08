#include "search_b1.h"
#include "internal.h"

#include <immintrin.h>

#include <stdio.h>

#ifdef INT32_SEARCH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(...)  ((void)0)
#endif

int32_t search_b1_find(const int32_t *vals, const uint16_t *lo16,
                       const int32_t *dir, size_t n, int32_t target,
                       size_t *out_index)
{
    if (n == 0 || vals == NULL || lo16 == NULL || dir == NULL)
        return INT32_SEARCH_ERR_INVALID_ARG;

    uint32_t h32 = (uint32_t)target >> 16;
    if (h32 >= 65536) return INT32_SEARCH_ERR_NOT_FOUND;

    uint16_t h = (uint16_t)h32 ^ 0x8000;
    int32_t start = dir[h];
    int32_t end   = (h < 65535) ? dir[h + 1] : (int32_t)n;
    /* D-143: defensive bounds clamp — corrupted dir protection.
     * start/end come from dir[] which is validated at build time,
     * but a defense-in-depth check catches all edge cases. */
    if (start < 0 || end < 0 || start >= end)
        return INT32_SEARCH_ERR_NOT_FOUND;
    if ((size_t)end > n) end = (int32_t)n;

    uint16_t target_lo16 = (uint16_t)(target & 0xFFFF);

    /* D-142: small-bucket scalar fast path — skips SIMD fixed overhead
     * (broadcast 3cy + cmpeq 1cy + movemask 3cy ≈ 7cy fixed cost).
     * Threshold 8: at 8 elements, scalar scan (8cy) breaks even with SIMD. */
    if (end - start < 8) {
        for (int32_t i = start; i < end; i++) {
            if (lo16[i] == target_lo16 && vals[i] == target) {
                if (out_index) *out_index = (size_t)i;
                return INT32_SEARCH_OK;
            }
        }
        return INT32_SEARCH_ERR_NOT_FOUND;
    }

    __m256i key = _mm256_set1_epi16((int16_t)target_lo16);

    int32_t i = start;

#ifdef INT32_SEARCH_B1_UNROLL2
    /* D-140: 2x unrolled SIMD loop — hides vpmovmskb 3-cycle latency.
     * Two independent cmpeq+movemask chains execute in parallel.
     * ⚠️ Requires -fno-unroll-loops to prevent GCC -O3 from further
     * unrolling which causes YMM register spill (~25% regression on
     * Windows MinGW).  Enable only under controlled compilation or
     * when PGO profile confirms benefit.  See meeting_017 follow-up. */
    for (; i + 32 <= end; i += 32) {
        __m256i c0 = _mm256_loadu_si256((const __m256i *)(lo16 + i));
        __m256i c1 = _mm256_loadu_si256((const __m256i *)(lo16 + i + 16));
        __m256i e0 = _mm256_cmpeq_epi16(key, c0);
        __m256i e1 = _mm256_cmpeq_epi16(key, c1);
        int m0 = _mm256_movemask_epi8(e0);
        int m1 = _mm256_movemask_epi8(e1);
        if ((m0 | m1) != 0) {
            if (m0 != 0) {
                int m = m0;
                while (m != 0) {
                    int bit2 = __builtin_ctz((unsigned int)m);
                    int32_t pos = i + (bit2 >> 1);
                    if (pos < end && vals[pos] == target) {
                        if (out_index) *out_index = (size_t)pos;
                        return INT32_SEARCH_OK;
                    }
                    m &= m - 1;
                }
            }
            if (m1 != 0) {
                int m = m1;
                while (m != 0) {
                    int bit2 = __builtin_ctz((unsigned int)m);
                    int32_t pos = i + 16 + (bit2 >> 1);
                    if (pos < end && vals[pos] == target) {
                        if (out_index) *out_index = (size_t)pos;
                        return INT32_SEARCH_OK;
                    }
                    m &= m - 1;
                }
            }
        }
    }

    /* Tail: single 16-element SIMD block for 16 ≤ remaining < 32 */
    if (i + 16 <= end) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)(lo16 + i));
        __m256i eq = _mm256_cmpeq_epi16(key, chunk);
        int mask = _mm256_movemask_epi8(eq);
        if (mask != 0) {
            int m = mask;
            while (m != 0) {
                int bit2 = __builtin_ctz((unsigned int)m);
                int32_t pos = i + (bit2 >> 1);
                if (pos < end && vals[pos] == target) {
                    if (out_index) *out_index = (size_t)pos;
                    return INT32_SEARCH_OK;
                }
                m &= m - 1;
            }
        }
        i += 16;
    }
#else
    /* Original single-issue 16-element SIMD loop.
     * D-141: lo16 is 32-byte aligned at base (platform_aligned_alloc).
     * Using _mm256_loadu_si256 (not _mm256_load_si256) because start
     * offset may not be 16-aligned; on aligned data, loadu == load on
     * Haswell+ with zero penalty. */
    for (; i + 16 <= end; i += 16) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)(lo16 + i));
        __m256i eq = _mm256_cmpeq_epi16(key, chunk);
        int mask = _mm256_movemask_epi8(eq);
        if (mask != 0) {
            int m = mask;
            while (m != 0) {
                int bit2 = __builtin_ctz((unsigned int)m);
                int32_t pos = i + (bit2 >> 1);
                if (pos < end && vals[pos] == target) {
                    if (out_index) *out_index = (size_t)pos;
                    return INT32_SEARCH_OK;
                }
                m &= m - 1;
            }
        }
    }
#endif

    /* Scalar tail for < 16 remaining */
    for (; i < end; i++) {
        if (lo16[i] == target_lo16 && vals[i] == target) {
            if (out_index) *out_index = (size_t)i;
            return INT32_SEARCH_OK;
        }
    }

    return INT32_SEARCH_ERR_NOT_FOUND;
}
