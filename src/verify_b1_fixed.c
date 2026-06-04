/*
 * verify_b1_fixed.c — B1 修复版正确性验证 (Step 1)
 *
 * 编译命令:
 *
 *   :: 性能测量用 (O3)
 *   gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
 *       src/verify_b1_fixed.c -o verify_b1_fixed
 *
 *   :: 正确性验证用 (ASan/UBSan, Linux/Mac)
 *   gcc -O1 -g -std=c11 -mavx2 -march=native -fsanitize=address,undefined \
 *       -fno-omit-frame-pointer src/verify_b1_fixed.c -o verify_b1_fixed_asan
 *
 *   :: Windows MinGW (不含 sanitizer)
 *   gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
 *       src/verify_b1_fixed.c -o verify_b1_fixed.exe
 *
 * 运行:
 *   verify_b1_fixed                  :: 标准正确性验证
 *   verify_b1_fixed --asan           :: ASan/UBSan 多规模测试
 *   verify_b1_fixed --bench          :: Benchmark (FIX-08, D-074)
 *
 * 已应用修复:
 *   FIX-01: popcount 去 ^ 0xFF, le_count → gt_count, 分支方向修复 (BUG-02)
 *   FIX-02: dir 后向填充显式哨兵 dir[65536] = (int32_t)n (BUG-01 防御性加固)
 *   FIX-03: h32 >= 65536 防御性检查 (BUG-03)
 *   FIX-04: dir_validate 增强: dir[0]==0 + 范围约束 dir[i]<=n
 */
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
#include <math.h>

#define DIR_BUCKETS 65537

static int g_failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
        g_failures++; \
    } else { \
        printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

static void *aligned_malloc(size_t size, size_t alignment) {
    return _mm_malloc(size, alignment);
}

static void aligned_free(void *ptr) {
    _mm_free(ptr);
}

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
    int32_t *data = (int32_t *)aligned_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)(lcg_next(seed) ^ (lcg_next(seed) << 11));
    qsort(data, n, sizeof(int32_t), compare_int32);
    return data;
}

static void generate_boundary_data(int32_t *buf, size_t n, unsigned int *seed) {
    int32_t *tmp = generate_sorted_data(n, seed);
    memcpy(buf, tmp, n * sizeof(int32_t));
    aligned_free(tmp);
}

static void shuffle(int32_t *arr, size_t n, unsigned int *seed) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)(lcg_next(seed) % (unsigned int)(i + 1));
        int32_t tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

static int32_t *generate_queries(const int32_t *data, size_t n, size_t num_queries,
                                  int hit_ratio_percent, unsigned int *seed) {
    int32_t *queries = (int32_t *)malloc(num_queries * sizeof(int32_t));
    if (!queries) { fprintf(stderr, "malloc failed\n"); exit(1); }
    size_t hit_count = (size_t)((uint64_t)num_queries * hit_ratio_percent / 100);
    for (size_t i = 0; i < hit_count; i++) {
        size_t idx = (size_t)lcg_next(seed) % n;
        queries[i] = data[idx];
    }
    for (size_t i = hit_count; i < num_queries; i++) {
        int32_t candidate;
        do {
            candidate = (int32_t)(lcg_next(seed) ^ (lcg_next(seed) << 11));
        } while (bsearch(&candidate, data, n, sizeof(int32_t), compare_int32) != NULL);
        queries[i] = candidate;
    }
    shuffle(queries, num_queries, seed);
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
    dir[DIR_BUCKETS - 1] = (int32_t)n;              /* FIX-02: 防御性哨兵 */
}

static int dir_validate(const int32_t *dir, size_t n) {
    if (dir[65536] != (int32_t)n) return 0;          /* 1. Sentinel */
    if (dir[0] != 0) return 0;                        /* 4. Start    FIX-04 */
    for (int i = 0; i < 65536; i++) {
        if (dir[i] > dir[i + 1]) return 0;            /* 2. Monotonicity */
        if (dir[i] < 0 || dir[i] > (int32_t)n)        /* 3. Range     FIX-04 */
            return 0;
    }
    return 1;
}

static int32_t search_b1_high16_lo16(const int32_t *vals, const uint16_t *lo16,
                                      const int32_t *dir, size_t n, int32_t target) {
    if (n == 0) return -1;
    uint32_t h32 = (uint32_t)target >> 16;             /* FIX-03 */
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

static int32_t search_avx2_binary(const int32_t *arr, size_t n, int32_t target) {
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
        int gt_count = __builtin_popcount(mask);        /* FIX-01 */

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

static void build_lo16(const int32_t *vals, size_t n, uint16_t *lo16) {
    for (size_t i = 0; i < n; i++)
        lo16[i] = (uint16_t)(vals[i] & 0xFFFF);
}

/* ================================================================
 * Benchmark Section (FIX-08, D-074)
 *
 * Method:
 *   - 6-round rotation (B1↔Scalar, 3x each order) to eliminate ordering bias
 *   - 2MB dummy pass between algorithms to flush L2 cache
 *   - 7 repeats per algorithm per round, discard first 3, median of last 4
 *   - 6 round-medians per algorithm → report overall median/stddev/min/max
 *   - Output: stdout + bench_b1_fixed.csv
 * ================================================================ */

static uint64_t rdtscp_bench(void) {
    unsigned int aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

static void bench_flush_cache(void) {
    size_t sz = 2 * 1024 * 1024;
    volatile char *buf = (volatile char *)malloc(sz);
    if (!buf) return;
    for (size_t i = 0; i < sz; i += 64)
        buf[i] = 0;
    free((void *)buf);
}

static int32_t *generate_sorted_skewed(size_t n, unsigned int *seed) {
    int32_t *data = (int32_t *)aligned_malloc(n * sizeof(int32_t), 32);
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

typedef struct {
    size_t max_sz;
    double weighted_avg;
} dir_stats_t;

static dir_stats_t dir_analyze(const int32_t *dir, size_t n) {
    dir_stats_t s = {0, 0.0};
    for (int i = 0; i < 65536; i++) {
        int32_t sz = dir[i + 1] - dir[i];
        if (sz > 0) {
            if ((size_t)sz > s.max_sz) s.max_sz = (size_t)sz;
            s.weighted_avg += (double)((uint64_t)sz * (uint64_t)sz);
        }
    }
    s.weighted_avg /= (double)n;
    return s;
}

typedef struct {
    double median;
    double min;
    double max;
    double stddev;
} bench_stats_t;

static bench_stats_t bench_compute_stats(double *samples, int n) {
    bench_stats_t s;
    double sorted[8];
    for (int i = 0; i < n; i++) sorted[i] = samples[i];
    for (int i = 1; i < n; i++) {
        double key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; j--; }
        sorted[j + 1] = key;
    }
    s.min = sorted[0];
    s.max = sorted[n - 1];
    if (n % 2 == 0)
        s.median = (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
    else
        s.median = sorted[n / 2];

    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += sorted[i];
    double mean = sum / (double)n;
    double sum_sq = 0.0;
    for (int i = 0; i < n; i++) {
        double d = sorted[i] - mean;
        sum_sq += d * d;
    }
    s.stddev = sqrt(sum_sq / (double)n);
    return s;
}

static bench_stats_t bench_round_b1(const int32_t *vals, const uint16_t *lo16,
                                     const int32_t *dir, size_t n,
                                     const int32_t *queries, size_t num_q) {
    double samples[7];
    for (int rep = 0; rep < 7; rep++) {
        bench_flush_cache();
        uint64_t t0 = rdtscp_bench();
        for (size_t q = 0; q < num_q; q++) {
            volatile int32_t r = search_b1_high16_lo16(vals, lo16, dir, n, queries[q]);
            (void)r;
        }
        uint64_t t1 = rdtscp_bench();
        samples[rep] = (double)(t1 - t0) / (double)num_q;
    }
    return bench_compute_stats(samples + 3, 4);
}

static bench_stats_t bench_round_scalar(const int32_t *vals, size_t n,
                                         const int32_t *queries, size_t num_q) {
    double samples[7];
    for (int rep = 0; rep < 7; rep++) {
        bench_flush_cache();
        uint64_t t0 = rdtscp_bench();
        for (size_t q = 0; q < num_q; q++) {
            volatile int32_t r = search_scalar_bs(vals, n, queries[q]);
            (void)r;
        }
        uint64_t t1 = rdtscp_bench();
        samples[rep] = (double)(t1 - t0) / (double)num_q;
    }
    return bench_compute_stats(samples + 3, 4);
}

static void bench_one_combo(const char *dist_name, size_t n, int is_skewed,
                            FILE *csv) {
    unsigned int seed = 12345;

    int32_t *data;
    if (is_skewed)
        data = generate_sorted_skewed(n, &seed);
    else
        data = generate_sorted_data(n, &seed);

    int32_t *dir = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    high16_dir_build(data, n, dir);
    if (!dir_validate(dir, n)) {
        fprintf(stderr, "FATAL: dir_validate failed n=%zu\n", n);
        aligned_free(dir); aligned_free(data); return;
    }

    dir_stats_t dstats = dir_analyze(dir, n);

    uint16_t *lo16 = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
    build_lo16(data, n, lo16);

    size_t num_q = 1000000;
    int32_t *queries = generate_queries(data, n, num_q, 50, &seed);

    printf("\n=== n=%.2fM  dist=%-7s  max_bucket=%-6zu  wavg=%.1f ===\n",
           n / 1000000.0, dist_name, dstats.max_sz, dstats.weighted_avg);

    double b1_meds[6], sc_meds[6];
    int b1_first = 1;

    for (int round = 0; round < 6; round++) {
        bench_stats_t b1_s, sc_s;
        if (b1_first) {
            b1_s = bench_round_b1(data, lo16, dir, n, queries, num_q);
            sc_s = bench_round_scalar(data, n, queries, num_q);
        } else {
            sc_s = bench_round_scalar(data, n, queries, num_q);
            b1_s = bench_round_b1(data, lo16, dir, n, queries, num_q);
        }
        b1_meds[round] = b1_s.median;
        sc_meds[round] = sc_s.median;
        b1_first = !b1_first;
    }

    bench_stats_t b1_final = bench_compute_stats(b1_meds, 6);
    bench_stats_t sc_final = bench_compute_stats(sc_meds, 6);
    double speedup = sc_final.median / b1_final.median;

    printf("  B1 (high16+lo16)    med=%7.1f  min=%7.1f  max=%7.1f  σ=%.1f  cy/q\n",
           b1_final.median, b1_final.min, b1_final.max, b1_final.stddev);
    printf("  Scalar binary       med=%7.1f  min=%7.1f  max=%7.1f  σ=%.1f  cy/q\n",
           sc_final.median, sc_final.min, sc_final.max, sc_final.stddev);
    printf("  B1 vs Scalar        %.2fx\n", speedup);

    if (csv) {
        fprintf(csv, "%zu,%s,%zu,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f\n",
                n, dist_name, dstats.max_sz, dstats.weighted_avg,
                b1_final.median, sc_final.median,
                b1_final.stddev, sc_final.stddev, speedup);
    }

    free(queries);
    aligned_free(lo16);
    aligned_free(dir);
    aligned_free(data);
}

static void run_benchmark_all(void) {
    printf("=== B1 Fixed Benchmark (FIX-08, D-074) ===\n");
    printf("Method: 6-round rotation | 2MB cache flush | 7 repeats "
           "(discard 3, median 4) | 6 round-medians\n");
    printf("Queries: 1M per combo, 50%% hit\n");
    printf("Sizes: 1.5M / 5M / 10M\n");
    printf("Distributions: uniform / skewed\n\n");

    FILE *csv = fopen("bench_b1_fixed.csv", "w");
    if (csv) {
        fprintf(csv, "n,dist,max_bucket,wavg,b1_cy_q,scalar_cy_q,"
                "b1_stddev,scalar_stddev,speedup\n");
    }

    size_t sizes[] = {1500000, 5000000, 10000000};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int si = 0; si < num_sizes; si++) {
        bench_one_combo("uniform", sizes[si], 0, csv);
        bench_one_combo("skewed",  sizes[si], 1, csv);
    }

    if (csv) fclose(csv);

    printf("\n=== Benchmark Complete ===\n");
    printf("Results saved to: bench_b1_fixed.csv\n");
}

static void verify_dir_build(void) {
    printf("\n--- [Step 1] dir_validate (D-066 4 checks) ---\n");
    unsigned int seed = 42;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);
    int32_t *dir  = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);

    high16_dir_build(data, n, dir);
    int ok = dir_validate(dir, n);
    CHECK(ok == 1, "dir_validate(data=%zu) returned %d", n, ok);

    aligned_free(dir);
    aligned_free(data);
}

static void verify_hit100(void) {
    printf("\n--- [Step 2] B1 命中率 100%% (n=100K) ---\n");
    unsigned int seed = 99;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);

    int32_t *dir = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    high16_dir_build(data, n, dir);
    if (!dir_validate(dir, n)) {
        CHECK(0, "dir_validate failed, cannot proceed");
        aligned_free(dir); aligned_free(data); return;
    }

    uint16_t *lo16 = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
    build_lo16(data, n, lo16);

    int32_t *queries = generate_queries(data, n, n, 100, &seed);

    int hits = 0;
    for (size_t q = 0; q < n; q++) {
        int32_t pos = search_b1_high16_lo16(data, lo16, dir, n, queries[q]);
        if (pos >= 0) hits++;
    }
    CHECK((size_t)hits == n, "100%% hit: hits=%d expected=%zu", hits, n);

    free(queries);
    aligned_free(lo16);
    aligned_free(dir);
    aligned_free(data);
}

static void verify_hit0(void) {
    printf("\n--- [Step 3] B1 命中率 0%% (n=100K) ---\n");
    unsigned int seed = 77;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);

    int32_t *dir = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    high16_dir_build(data, n, dir);
    if (!dir_validate(dir, n)) {
        CHECK(0, "dir_validate failed, cannot proceed");
        aligned_free(dir); aligned_free(data); return;
    }

    uint16_t *lo16 = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
    build_lo16(data, n, lo16);

    int32_t *queries = generate_queries(data, n, n, 0, &seed);

    int hits = 0;
    for (size_t q = 0; q < n; q++) {
        int32_t pos = search_b1_high16_lo16(data, lo16, dir, n, queries[q]);
        if (pos >= 0) hits++;
    }
    CHECK(hits == 0, "0%% hit: hits=%d expected=0", hits);

    free(queries);
    aligned_free(lo16);
    aligned_free(dir);
    aligned_free(data);
}

static void verify_cross_check(void) {
    printf("\n--- [Step 4] B1 vs Scalar 交叉验证 (n=100K, 50%% hit) ---\n");
    unsigned int seed = 55;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);

    int32_t *dir = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    high16_dir_build(data, n, dir);
    if (!dir_validate(dir, n)) {
        CHECK(0, "dir_validate failed, cannot proceed");
        aligned_free(dir); aligned_free(data); return;
    }

    uint16_t *lo16 = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
    build_lo16(data, n, lo16);

    size_t num_q = n;
    int32_t *queries = generate_queries(data, n, num_q, 50, &seed);

    int mismatches = 0;
    int32_t first_mismatch_target = 0;
    size_t first_mismatch_idx = 0;
    for (size_t q = 0; q < num_q; q++) {
        int32_t b1_pos = search_b1_high16_lo16(data, lo16, dir, n, queries[q]);
        int32_t sc_pos = search_scalar_bs(data, n, queries[q]);
        int b1_found = (b1_pos >= 0) ? 1 : 0;
        int sc_found = (sc_pos >= 0) ? 1 : 0;
        if (b1_found != sc_found) {
            if (mismatches == 0) {
                first_mismatch_target = queries[q];
                first_mismatch_idx = q;
            }
            mismatches++;
        }
    }
    CHECK(mismatches == 0,
          "B1 vs scalar: %d mismatches (first: target=%d idx=%zu)",
          mismatches, first_mismatch_target, first_mismatch_idx);

    mismatches = 0;
    for (size_t q = 0; q < num_q; q++) {
        int32_t b1_pos = search_b1_high16_lo16(data, lo16, dir, n, queries[q]);
        void *bs = bsearch(&queries[q], data, n, sizeof(int32_t), compare_int32);
        int b1_found = (b1_pos >= 0) ? 1 : 0;
        int bs_found = (bs != NULL) ? 1 : 0;
        if (b1_found != bs_found) {
            if (mismatches == 0) {
                first_mismatch_target = queries[q];
                first_mismatch_idx = q;
            }
            mismatches++;
        }
    }
    CHECK(mismatches == 0,
          "B1 vs bsearch: %d mismatches (first: target=%d idx=%zu)",
          mismatches, first_mismatch_target, first_mismatch_idx);

    free(queries);
    aligned_free(lo16);
    aligned_free(dir);
    aligned_free(data);
}

static void verify_boundary_keys(void) {
    printf("\n--- [Step 5] 边界 key 验证 ---\n");

    unsigned int seed = 33;
    size_t n = 100000;

    int32_t *data = (int32_t *)aligned_malloc(n * sizeof(int32_t), 32);
    generate_boundary_data(data, n, &seed);

    int32_t *dir = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    high16_dir_build(data, n, dir);
    if (!dir_validate(dir, n)) {
        CHECK(0, "dir_validate failed, cannot proceed");
        aligned_free(dir); aligned_free(data); return;
    }

    uint16_t *lo16 = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
    build_lo16(data, n, lo16);

    int32_t boundary_keys[] = { INT32_MIN, -1, 0, 1, INT32_MAX };
    const char *boundary_names[] = { "INT32_MIN", "-1", "0", "1", "INT32_MAX" };
    int num_keys = sizeof(boundary_keys) / sizeof(boundary_keys[0]);

    for (int k = 0; k < num_keys; k++) {
        int32_t target = boundary_keys[k];
        int32_t b1_pos = search_b1_high16_lo16(data, lo16, dir, n, target);
        int32_t sc_pos = search_scalar_bs(data, n, target);

        if (b1_pos >= 0 && sc_pos >= 0) {
            CHECK(data[b1_pos] == target,
                  "boundary %s: B1 found at %d (val=%d == target=%d)",
                  boundary_names[k], b1_pos, data[b1_pos], target);
        } else if (b1_pos < 0 && sc_pos < 0) {
            printf("  PASS: boundary %s: both B1 and scalar return -1 (not found)\n",
                   boundary_names[k]);
        } else {
            CHECK(0, "boundary %s: B1=%d scalar=%d MISMATCH",
                  boundary_names[k], b1_pos, sc_pos);
        }
    }

    aligned_free(lo16);
    aligned_free(dir);
    aligned_free(data);
}

static void verify_asan_sizes(const int *sizes, int num_sizes) {
    unsigned int seed = 1;
    for (int si = 0; si < num_sizes; si++) {
        size_t n = (size_t)sizes[si];
        printf("\n--- [ASan] n=%zu ---\n", n);

        int32_t *data = NULL;
        int32_t *dir  = NULL;
        uint16_t *lo16 = NULL;
        int32_t *queries = NULL;

        if (n > 0) {
            data = (int32_t *)aligned_malloc(n * sizeof(int32_t), 32);
            if (!data) { printf("  SKIP: malloc failed\n"); continue; }
            generate_boundary_data(data, n, &seed);

            dir = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
            high16_dir_build(data, n, dir);

            int ok = dir_validate(dir, n);
            CHECK(ok == 1, "dir_validate n=%zu: %d", n, ok);
            if (!ok) { aligned_free(data); aligned_free(dir); continue; }

            lo16 = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
            build_lo16(data, n, lo16);

            size_t num_q = n > 1000 ? 1000 : n;
            if (num_q == 0) num_q = 1;
            queries = (int32_t *)malloc(num_q * sizeof(int32_t));
            for (size_t i = 0; i < num_q; i++) queries[i] = data[lcg_next(&seed) % n];

            for (size_t q = 0; q < num_q; q++) {
                volatile int32_t r;
                r = search_b1_high16_lo16(data, lo16, dir, n, queries[q]);
                (void)r;
                r = search_avx2_binary(data, n, queries[q]);
                (void)r;
                r = search_scalar_bs(data, n, queries[q]);
            }

            free(queries);
            aligned_free(lo16);
            aligned_free(dir);
            aligned_free(data);
        } else {
            volatile int32_t r;
            r = search_b1_high16_lo16(NULL, NULL, NULL, 0, 0);
            (void)r;
            r = search_avx2_binary(NULL, 0, 0);
            (void)r;
            r = search_scalar_bs(NULL, 0, 0);
            printf("  PASS: n=0 (early return) all OK\n");
        }
    }
}

int main(int argc, char **argv) {
    int asan_only = 0;
    int bench = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--asan") == 0) asan_only = 1;
        if (strcmp(argv[i], "--bench") == 0) bench = 1;
    }

    if (bench) {
        run_benchmark_all();
        return 0;
    }

    printf("=== B1 Fixed Correctness Verification ===\n");
    printf("FIX-01~04 applied: popcount / dir-sentinel / sign-ext / dir-validate\n\n");

    if (asan_only) {
        int sizes[] = {0,1,5,63,64,65,256,65535,65536,65537,100000};
        int num = sizeof(sizes) / sizeof(sizes[0]);
        printf("ASan/UBSan mode: testing %d sizes\n", num);
        verify_asan_sizes(sizes, num);
    } else {
        verify_dir_build();
        verify_hit100();
        verify_hit0();
        verify_cross_check();
        verify_boundary_keys();
    }

    printf("\n=== %s: %d failures ===\n",
           g_failures == 0 ? "ALL PASSED" : "FAILURES",
           g_failures);
    return g_failures > 0 ? 1 : 0;
}
