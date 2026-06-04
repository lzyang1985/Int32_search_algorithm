#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <immintrin.h>

#define DIR_BUCKETS 65537

#define B1_POOL_DIR_SIZE      262148
#define B1_POOL_LO16_OFFSET   262176
#define B1_POOL_TOTAL_SIZE(n) (B1_POOL_LO16_OFFSET + 2 * (size_t)(n))

#define B1_POOL_DIR(pool)     ((const int32_t *)(pool))
#define B1_POOL_LO16(pool)    ((const uint16_t *)((const char *)(pool) + B1_POOL_LO16_OFFSET))

static int g_failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
        g_failures++; \
    } else { \
        printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

static int compare_int32(const void *a, const void *b) {
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static unsigned int lcg_next(unsigned int *seed) {
    *seed = *seed * 1103515245u + 12345u;
    return *seed >> 16;
}

static int32_t *generate_sorted_data(size_t n, unsigned int *seed) {
    int32_t *data = (int32_t *)_mm_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)(lcg_next(seed) ^ (lcg_next(seed) << 11));
    qsort(data, n, sizeof(int32_t), compare_int32);
    return data;
}

static int32_t *generate_sorted_skewed(size_t n, unsigned int *seed) {
    int32_t *data = (int32_t *)_mm_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    size_t cluster = n * 80 / 100;
    uint64_t range_lo = (uint64_t)((uint32_t)INT32_MAX / 5 + 1);
    for (size_t i = 0; i < cluster; i++) {
        uint32_t r = lcg_next(seed) ^ (lcg_next(seed) << 11);
        data[i] = INT32_MIN + (int32_t)((uint64_t)r % range_lo);
    }
    for (size_t i = cluster; i < n; i++) {
        uint32_t r = lcg_next(seed) ^ (lcg_next(seed) << 11);
        uint64_t base = (uint64_t)INT32_MIN + range_lo;
        data[i] = (int32_t)(base + (uint64_t)(r % ((uint32_t)INT32_MAX - (uint32_t)range_lo + 1)));
    }
    qsort(data, n, sizeof(int32_t), compare_int32);
    return data;
}

static void high16_dir_build(const int32_t *vals, size_t n, int32_t *dir) {
    for (size_t i = 0; i <= DIR_BUCKETS - 1; i++) dir[i] = -1;
    dir[DIR_BUCKETS - 1] = (int32_t)n;
    int first = 1;
    uint16_t prev_h = 0;
    for (size_t i = 0; i < n; i++) {
        uint16_t h = (uint16_t)((uint32_t)vals[i] >> 16);
        if (first || h != prev_h) {
            dir[h] = (int32_t)i;
            prev_h = h;
            first = 0;
        }
    }
    for (int32_t i = DIR_BUCKETS - 2; i >= 0; i--) {
        if (dir[i] == -1) dir[i] = dir[i + 1];
    }
    dir[DIR_BUCKETS - 1] = (int32_t)n;
}

static int dir_validate(const int32_t *dir, size_t n) {
    if (dir[65536] != (int32_t)n) return 0;
    if (dir[0] != 0) return 0;
    for (int i = 0; i < 65536; i++) {
        if (dir[i] > dir[i + 1]) return 0;
        if (dir[i] < 0 || dir[i] > (int32_t)n) return 0;
    }
    return 1;
}

static void build_lo16(const int32_t *vals, size_t n, uint16_t *lo16) {
    for (size_t i = 0; i < n; i++)
        lo16[i] = (uint16_t)(vals[i] & 0xFFFF);
}

static int32_t search_scalar_bs(const int32_t *vals, size_t n, int32_t target) {
    if (n == 0) return -1;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && vals[lo] == target) return (int32_t)lo;
    return -1;
}

static int32_t search_b1_3ptr(const int32_t *vals, const uint16_t *lo16,
                               const int32_t *dir, size_t n, int32_t target) {
    if (n == 0) return -1;
    uint32_t h32 = (uint32_t)target >> 16;
    if (h32 >= 65536) return -1;
    uint16_t h = (uint16_t)h32;
    int32_t start = dir[h];
    int32_t end   = (h < 65535) ? dir[h + 1] : (int32_t)n;
    if (start >= end) return -1;
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
                if (pos < end && vals[pos] == target) return pos;
                m &= m - 1;
            }
        }
    }
    for (; i < end; i++) {
        if (lo16[i] == target_lo16 && vals[i] == target) return i;
    }
    return -1;
}

static uint8_t *b1_pool_build(const int32_t *vals, size_t n) {
    int32_t temp_dir[DIR_BUCKETS];
    high16_dir_build(vals, n, temp_dir);
    if (!dir_validate(temp_dir, n)) return NULL;

    size_t total = B1_POOL_TOTAL_SIZE(n);
    uint8_t *pool = (uint8_t *)_mm_malloc(total, 32);
    if (!pool) return NULL;

    memcpy(pool, temp_dir, sizeof(temp_dir));
    memset(pool + B1_POOL_DIR_SIZE, 0, B1_POOL_LO16_OFFSET - B1_POOL_DIR_SIZE);

    uint16_t *lo16 = (uint16_t *)(pool + B1_POOL_LO16_OFFSET);
    build_lo16(vals, n, lo16);

    return pool;
}

static int32_t search_b1_pool(const uint8_t *pool, const int32_t *vals, size_t n, int32_t target) {
    if (n == 0 || pool == NULL) return -1;
    uint32_t h32 = (uint32_t)target >> 16;
    if (h32 >= 65536) return -1;
    uint16_t h = (uint16_t)h32;

    const int32_t  *dir  = B1_POOL_DIR(pool);
    const uint16_t *lo16 = B1_POOL_LO16(pool);

    int32_t start = dir[h];
    int32_t end   = (h < 65535) ? dir[h + 1] : (int32_t)n;
    if (start >= end) return -1;

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
                if (pos < end && vals[pos] == target) return pos;
                m &= m - 1;
            }
        }
    }
    for (; i < end; i++) {
        if (lo16[i] == target_lo16 && vals[i] == target) return i;
    }
    return -1;
}

static void b1_pool_destroy(uint8_t *pool) {
    _mm_free(pool);
}

static void test_size(size_t n, const char *dist_name, int is_skewed) {
    unsigned int seed = 12345;
    int32_t *data;
    if (is_skewed)
        data = generate_sorted_skewed(n, &seed);
    else
        data = generate_sorted_data(n, &seed);

    int32_t dir[DIR_BUCKETS];
    high16_dir_build(data, n, dir);
    CHECK(dir_validate(dir, n) == 1,
          "[n=%zu %s] dir_validate passed", n, dist_name);

    uint16_t *lo16 = (uint16_t *)_mm_malloc(n * sizeof(uint16_t), 32);
    if (!lo16) { fprintf(stderr, "FATAL: lo16 malloc failed n=%zu\n", n); _mm_free(data); return; }
    build_lo16(data, n, lo16);

    int32_t test_keys[4];
    int num_keys = 0;
    if (n > 0) {
        test_keys[num_keys++] = data[0];
        test_keys[num_keys++] = data[n - 1];
        if (n > 2)
            test_keys[num_keys++] = data[n / 2];
    }
    test_keys[num_keys++] = INT32_MIN;

    for (int k = 0; k < num_keys; k++) {
        int32_t target = test_keys[k];
        int32_t b1_pos  = search_b1_3ptr(data, lo16, dir, n, target);
        int32_t sc_pos  = search_scalar_bs(data, n, target);
        int b1_found = (b1_pos >= 0) ? 1 : 0;
        int sc_found = (sc_pos >= 0) ? 1 : 0;
        CHECK(b1_found == sc_found,
              "[n=%zu %s] 3ptr vs scalar agree on key[%d]", n, dist_name, k);
    }

    uint8_t *pool = b1_pool_build(data, n);
    if (n == 0) {
        if (pool) {
            CHECK(search_b1_pool(pool, data, n, INT32_MIN) == -1,
                  "[n=%zu %s] pool search INT32_MIN returns -1 for empty", n, dist_name);
            b1_pool_destroy(pool);
        }
    } else {
        CHECK(pool != NULL, "[n=%zu %s] pool_build success", n, dist_name);
        if (pool) {
            for (int k = 0; k < num_keys; k++) {
                int32_t target = test_keys[k];
                int32_t pool_pos = search_b1_pool(pool, data, n, target);
                int32_t sc_pos   = search_scalar_bs(data, n, target);
                int pool_found = (pool_pos >= 0) ? 1 : 0;
                int sc_found   = (sc_pos >= 0) ? 1 : 0;
                CHECK(pool_found == sc_found,
                      "[n=%zu %s] pool vs scalar agree on key[%d]", n, dist_name, k);
            }
            b1_pool_destroy(pool);
        }
    }

    _mm_free(lo16);
    _mm_free(data);
}

int main(void) {
    printf("=== B1 ASan Edge Case Verification ===\n\n");

    printf("--- SV-05: BUG-04 regression (n=0,1,5,63,64,65) ---\n");
    size_t small_sizes[] = {0, 1, 5, 63, 64, 65};
    int num_small = (int)(sizeof(small_sizes) / sizeof(small_sizes[0]));
    for (int i = 0; i < num_small; i++) {
        printf("\n  n=%zu uniform:\n", small_sizes[i]);
        test_size(small_sizes[i], "uniform", 0);
        printf("  n=%zu skewed:\n", small_sizes[i]);
        test_size(small_sizes[i], "skewed", 1);
    }

    printf("\n--- SV-04: boundary sizes (n=65535,65536,65537) ---\n");
    size_t boundary_sizes[] = {65535, 65536, 65537};
    int num_boundary = (int)(sizeof(boundary_sizes) / sizeof(boundary_sizes[0]));
    for (int i = 0; i < num_boundary; i++) {
        printf("\n  n=%zu uniform:\n", boundary_sizes[i]);
        test_size(boundary_sizes[i], "uniform", 0);
        printf("  n=%zu skewed:\n", boundary_sizes[i]);
        test_size(boundary_sizes[i], "skewed", 1);
    }

    printf("\n=== %s: %d failures ===\n",
           g_failures == 0 ? "ALL ASAN EDGE TESTS PASSED" : "FAILURES",
           g_failures);
    return g_failures > 0 ? 1 : 0;
}
