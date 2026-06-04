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
    if (start >= end) return INT32_SEARCH_ERR_NOT_FOUND;

    uint16_t target_lo16 = (uint16_t)(target & 0xFFFF);
    __m256i key = _mm256_set1_epi16((int16_t)target_lo16);

    int32_t i = start;
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

    for (; i < end; i++) {
        if (lo16[i] == target_lo16 && vals[i] == target) {
            if (out_index) *out_index = (size_t)i;
            return INT32_SEARCH_OK;
        }
    }

    return INT32_SEARCH_ERR_NOT_FOUND;
}
