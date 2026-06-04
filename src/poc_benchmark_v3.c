#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <immintrin.h>

#define DIR_BUCKETS 65537

static void *aligned_malloc(size_t size, size_t alignment) {
    return _mm_malloc(size, alignment);
}

static void aligned_free(void *ptr) {
    _mm_free(ptr);
}

static uint64_t rdtscp_bench(void) {
    unsigned int aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

static int compare_int32(const void *a, const void *b) {
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static int32_t *generate_sorted_data(size_t n) {
    int32_t *data = (int32_t *)aligned_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)((uint32_t)rand() ^ ((uint32_t)rand() << 15));
    qsort(data, n, sizeof(int32_t), compare_int32);
    return data;
}

static void shuffle(int32_t *arr, size_t n) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)rand() % (i + 1);
        int32_t tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

static int32_t *generate_queries(const int32_t *data, size_t n, size_t num_queries, int hit_ratio_percent) {
    int32_t *queries = (int32_t *)malloc(num_queries * sizeof(int32_t));
    if (!queries) { fprintf(stderr, "malloc failed\n"); exit(1); }
    size_t hit_count = (size_t)((uint64_t)num_queries * hit_ratio_percent / 100);
    for (size_t i = 0; i < hit_count; i++) {
        size_t idx = (size_t)rand() % n;
        queries[i] = data[idx];
    }
    for (size_t i = hit_count; i < num_queries; i++) {
        int32_t candidate;
        do {
            candidate = (int32_t)((uint32_t)rand() ^ ((uint32_t)rand() << 15));
        } while (bsearch(&candidate, data, n, sizeof(int32_t), compare_int32) != NULL);
        queries[i] = candidate;
    }
    shuffle(queries, num_queries);
    return queries;
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

typedef struct {
    size_t max_sz;
    double weighted_avg;
    size_t total_n;
} dir_stats_t;

static dir_stats_t dir_analyze(const int32_t *dir, size_t n) {
    dir_stats_t s = {0, 0.0, 0};
    for (int i = 0; i < 65536; i++) {
        int32_t sz = dir[i + 1] - dir[i];
        if (sz > 0) {
            if ((size_t)sz > s.max_sz) s.max_sz = (size_t)sz;
            s.weighted_avg += (double)((uint64_t)sz * (uint64_t)sz);
            s.total_n += (size_t)sz;
        }
    }
    s.weighted_avg /= (double)n;
    return s;
}

int32_t search_avx2_binary(const int32_t *arr, size_t n, int32_t target) {
    if (n == 0) return -1;
    size_t lo = 0, hi = n;

    while (hi - lo >= 8) {
        size_t mid = lo + (hi - lo) / 2;
        size_t block = mid & ~(size_t)7;
        if (block + 8 > hi) block = hi - 8;

        __m256i k = _mm256_set1_epi32(target);
        __m256i v = _mm256_loadu_si256((const __m256i *)(arr + block));
        __m256i cmp = _mm256_cmpgt_epi32(k, v);
        int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        int gt_count = __builtin_popcount(mask);

        if (gt_count == 0) {
            hi = block;
        } else if (gt_count == 8) {
            lo = block + 8;
        } else {
            size_t idx = block + (size_t)gt_count;
            if (arr[idx] == target) return (int32_t)idx;
            lo = block + (size_t)gt_count;
            hi = block + (size_t)gt_count;
        }
    }

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && arr[lo] == target) return (int32_t)lo;
    return -1;
}

int32_t search_b1_high16_lo16(const int32_t *vals, const uint16_t *lo16, const int32_t *dir,
                               size_t n, int32_t target) {
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

typedef int32_t (*search_fn)(void *ctx, int32_t target);

typedef struct {
    const int32_t *arr;
    size_t n;
} avx2_ctx_t;

static int32_t bench_avx2(void *ctx, int32_t target) {
    avx2_ctx_t *c = (avx2_ctx_t *)ctx;
    return search_avx2_binary(c->arr, c->n, target);
}

typedef struct {
    const int32_t *vals;
    const uint16_t *lo16;
    const int32_t *dir;
    size_t n;
} b1_ctx_t;

static int32_t bench_b1(void *ctx, int32_t target) {
    b1_ctx_t *c = (b1_ctx_t *)ctx;
    return search_b1_high16_lo16(c->vals, c->lo16, c->dir, c->n, target);
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

typedef struct {
    const int32_t *arr;
    size_t n;
} scalar_ctx_t;

static int32_t bench_scalar(void *ctx, int32_t target) {
    scalar_ctx_t *c = (scalar_ctx_t *)ctx;
    return search_scalar_bs(c->arr, c->n, target);
}

static double run_one(const char *label, search_fn fn, void *ctx,
                      const int32_t *queries, size_t num_queries) {
    int num_warmup = num_queries > 100 ? 100 : (int)num_queries;
    volatile int32_t discard = 0;
    for (int w = 0; w < num_warmup; w++)
        discard |= fn(ctx, queries[w]);
    (void)discard;

    uint64_t t0 = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++)
        discard |= fn(ctx, queries[q]);
    uint64_t t1 = rdtscp_bench();
    (void)discard;

    double cy = (double)(t1 - t0) / (double)num_queries;
    printf("  %-30s  %8.1f cy/q\n", label, cy);
    fflush(stdout);
    return cy;
}

static void run_crossover_test(size_t n) {
    printf("\n========================================\n");
    printf("  Crossover Test: N = %zu (%.2fM)\n", n, n / 1000000.0);
    printf("========================================\n");

    int32_t *data = generate_sorted_data(n);

    int32_t *dir = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    high16_dir_build(data, n, dir);

    if (!dir_validate(dir, n)) {
        printf("  *** DIR VALIDATION FAILED ***\n");
        aligned_free(dir);
        aligned_free(data);
        return;
    }

    dir_stats_t stats = dir_analyze(dir, n);
    printf("  Distribution: max_bucket=%zu  weighted_avg=%.1f\n",
           stats.max_sz, stats.weighted_avg);

    const char *decision;
    if ((double)stats.max_sz > 0.1 * (double)n) {
        decision = "PATH_A (skewed)";
    } else if (stats.weighted_avg <= 45.0) {
        decision = "PATH_B1";
    } else {
        decision = "PATH_A";
    }
    printf("  D-015 decision: %s\n", decision);

    size_t num_queries = 1000000;
    int32_t *queries = generate_queries(data, n, num_queries, 50);

    avx2_ctx_t avx2_ctx = { data, n };
    double a_cy = run_one("A: AVX2 SIMD binary", bench_avx2, &avx2_ctx,
                           queries, num_queries);

    scalar_ctx_t scalar_ctx = { data, n };
    double s_cy = run_one("Scalar binary search", bench_scalar, &scalar_ctx,
                           queries, num_queries);

    uint16_t *lo16 = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
    for (size_t i = 0; i < n; i++)
        lo16[i] = (uint16_t)(data[i] & 0xFFFF);

    b1_ctx_t b1_ctx = { data, lo16, dir, n };
    double b1_cy = run_one("B1: high16dir + lo16 scan", bench_b1, &b1_ctx,
                            queries, num_queries);

    printf("  ──────────────────────────────────────\n");
    printf("  B1 / A ratio:    %.2fx\n", b1_cy / a_cy);
    printf("  B1 vs Scalar:    %.2fx\n", s_cy / b1_cy);
    printf("  A vs Scalar:     %.2fx\n", s_cy / a_cy);
    printf("  Scalar:          %.1f cy/q\n", s_cy);
    printf("  WINNER:          %s\n",
           b1_cy < a_cy ? (b1_cy < s_cy ? "B1" : "Scalar") :
                          (a_cy < s_cy ? "A" : "Scalar"));

    aligned_free(lo16);
    free(queries);
    aligned_free(dir);
    aligned_free(data);
}

int main(void) {
    srand((unsigned int)time(NULL));

    printf("Int32 Search Algorithm POC Benchmark v3\n");
    printf("Crossover calibration: B1 vs A vs Scalar at 1.5M / 5M / 10M\n");
    printf("Compiled: gcc -O3 -mavx2 -march=native\n");
    printf("Distribution: uniform random\n");
    printf("Queries: 1M per test, 50%% hit rate\n");
    printf("D-015 rule: max_sz > 0.1*n ? PATH_A : (weighted_avg <= 45 ? PATH_B1 : PATH_A)\n\n");

    run_crossover_test(1500000);
    run_crossover_test(5000000);
    run_crossover_test(10000000);

    printf("\nDone.\n");
    return 0;
}
