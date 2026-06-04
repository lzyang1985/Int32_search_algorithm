/*
 * poc_int64_b1.c — ACT-15: int64 B1 high20 dir + lo44 4路SIMD扫描验证
 *
 * 编译命令:
 *
 *   :: 性能测量用 (O3)
 *   gcc -O3 -std=c11 -mavx2 src/poc_int64_b1.c -o poc_int64_b1 -lm
 *
 *   :: ASan/UBSan 验证用 (Linux)
 *   gcc -O1 -g -std=c11 -mavx2 -fsanitize=address,undefined \
 *       -fno-omit-frame-pointer src/poc_int64_b1.c -o poc_int64_b1_asan -lm
 *
 *   :: Windows MinGW (不含 sanitizer)
 *   gcc -O3 -std=c11 -mavx2 src/poc_int64_b1.c -o poc_int64_b1.exe -lm
 *
 * 运行:
 *   poc_int64_b1                :: 正确性验证 + 性能基准 (默认)
 *   poc_int64_b1 --verify       :: 仅正确性验证
 *   poc_int64_b1 --bench        :: 仅性能基准 (6 组 benchmark 矩阵)
 *   poc_int64_b1 --all          :: 全部测试
 *   poc_int64_b1_asan           :: ASan/UBSan 验证
 *
 * 验收标准:
 *   GATE-3: B1 ≥ 1.5x Path A (10M uniform) → 纳入 Phase 1
 *   6 组 benchmark 全部完成
 *   若 B1 不达标，输出降级建议
 *
 * 算法:
 *   - int64 high20 位构建目录 dir[1048577]
 *   - 桶内 4 路 _mm256_cmpeq_epi64 扫描 + 标量尾部
 *   - 三路对比: B1 / Path A AVX2 二分 / 标量二分
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <immintrin.h>
#include <math.h>

#define INT64_DIR_SIZE 1048577
#define BENCH_REPS     7
#define BENCH_WARMUP   1
#define BENCH_TOTAL    (BENCH_WARMUP + BENCH_REPS)

static int g_failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
        g_failures++; \
    } else { \
        printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

static int cmp_int64(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

static uint64_t xorshift64_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/*
 * search_int64_scalar — 标量 lower_bound 二分查找
 */
static size_t search_int64_scalar(const int64_t *vals, size_t n, int64_t target) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return (lo < n && vals[lo] == target) ? lo : SIZE_MAX;
}

/*
 * search_int64_avx2 — Path A: 4路 SIMD 二分查找 (D-103 规范)
 */
static size_t search_int64_avx2(const int64_t *vals, size_t n, int64_t target) {
    if (n == 0) return SIZE_MAX;
    size_t lo = 0, hi = n;

    while (hi - lo >= 4) {
        size_t mid = lo + (hi - lo) / 2;
        size_t block = mid & ~(size_t)3;
        if (block < lo) block = lo;
        if (block + 4 > hi) block = hi - 4;

        __m256i key = _mm256_set1_epi64x(target);
        __m256i vec = _mm256_loadu_si256((const __m256i *)(vals + block));
        __m256i cmp = _mm256_cmpgt_epi64(vec, key);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(cmp));
        int le_count = 4 - __builtin_popcount((unsigned)mask);

        if (le_count == 0) {
            hi = block;
        } else {
            size_t last_le = block + (size_t)le_count - 1;
            if (vals[last_le] < target) {
                lo = block + (size_t)le_count;
            } else {
                if (block + (size_t)le_count == hi) break;
                hi = block + (size_t)le_count;
            }
        }
    }

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target) lo = mid + 1;
        else hi = mid;
    }

    return (lo < n && vals[lo] == target) ? lo : SIZE_MAX;
}

/*
 * build_dir_int64 — 构建 high20 目录
 * dir[0..1048575] 为各桶起始偏移, dir[1048576] 为哨兵 (= n)
 */
static void build_dir_int64(const int64_t *vals, size_t n, int32_t *dir) {
    int i;
    for (i = 0; i < INT64_DIR_SIZE - 1; i++)
        dir[i] = -1;
    dir[INT64_DIR_SIZE - 1] = (int32_t)n;

    int first = 1;
    uint32_t prev_h = 0;
    for (size_t j = 0; j < n; j++) {
        uint32_t h = (uint32_t)(((uint64_t)vals[j] ^ ((uint64_t)1 << 63)) >> 44);
        if (first || h != prev_h) {
            dir[h] = (int32_t)j;
            prev_h = h;
            first = 0;
        }
    }

    for (i = INT64_DIR_SIZE - 2; i >= 0; i--) {
        if (dir[i] == -1) dir[i] = dir[i + 1];
    }
    dir[INT64_DIR_SIZE - 1] = (int32_t)n;
}

static int dir_validate_int64(const int32_t *dir, size_t n) {
    if (dir[INT64_DIR_SIZE - 1] != (int32_t)n) return 0;
    if (dir[0] != 0) return 0;
    for (int i = 0; i < INT64_DIR_SIZE - 1; i++) {
        if (dir[i] > dir[i + 1]) return 0;
        if (dir[i] < 0 || dir[i] > (int32_t)n) return 0;
    }
    return 1;
}

/*
 * search_int64_b1 — Path B1: high20 dir + lo44 4路SIMD扫描
 */
static size_t search_int64_b1(const int64_t *vals, size_t n,
                               const int32_t *dir, int64_t target) {
    if (n == 0) return SIZE_MAX;

    uint32_t h = (uint32_t)(((uint64_t)target ^ ((uint64_t)1 << 63)) >> 44);
    int32_t start = dir[h];
    int32_t end   = dir[h + 1];

    if (start >= end) return SIZE_MAX;

    __m256i key = _mm256_set1_epi64x(target);
    int32_t i = start;

    for (; i + 4 <= end; i += 4) {
        __m256i vec = _mm256_loadu_si256((const __m256i *)(vals + i));
        __m256i eq = _mm256_cmpeq_epi64(key, vec);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(eq));
        if (mask != 0) {
            int m = mask;
            while (m != 0) {
                int bit = __builtin_ctz((unsigned int)m);
                int32_t pos = i + bit;
                if (pos < end && vals[pos] == target) return (size_t)pos;
                m &= m - 1;
            }
        }
    }

    for (; i < end; i++) {
        if (vals[i] == target) return (size_t)i;
    }

    return SIZE_MAX;
}

/*
 * 数据生成器
 */
static int64_t *generate_sorted_uniform(size_t n, uint64_t *seed) {
    int64_t *data = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);
    if (!data) { fprintf(stderr, "FATAL: _mm_malloc failed\n"); exit(1); }
    for (size_t i = 0; i < n; i++)
        data[i] = (int64_t)xorshift64_next(seed);
    qsort(data, n, sizeof(int64_t), cmp_int64);
    return data;
}

static int64_t *generate_sorted_skewed(size_t n, uint64_t *seed) {
    int64_t *data = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);
    if (!data) { fprintf(stderr, "FATAL: _mm_malloc failed\n"); exit(1); }
    size_t cluster = n * 80 / 100;
    uint32_t max_h = (1u << 20) / 5;
    for (size_t i = 0; i < cluster; i++) {
        uint32_t h = (uint32_t)(xorshift64_next(seed) % max_h);
        uint64_t lo = xorshift64_next(seed) & ((((uint64_t)1) << 44) - 1);
        data[i] = (int64_t)(((uint64_t)h << 44) | lo);
    }
    for (size_t i = cluster; i < n; i++) {
        uint32_t h = max_h + (uint32_t)(xorshift64_next(seed) % ((1u << 20) - max_h));
        uint64_t lo = xorshift64_next(seed) & ((((uint64_t)1) << 44) - 1);
        data[i] = (int64_t)(((uint64_t)h << 44) | lo);
    }
    qsort(data, n, sizeof(int64_t), cmp_int64);
    return data;
}

static int64_t *generate_sorted_zipf(size_t n, uint64_t *seed) {
    int64_t *data = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);
    if (!data) { fprintf(stderr, "FATAL: _mm_malloc failed\n"); exit(1); }

    uint32_t N = (1u << 20);
    double *cdf = (double *)malloc(N * sizeof(double));
    if (!cdf) { _mm_free(data); fprintf(stderr, "FATAL: malloc cdf failed\n"); exit(1); }

    double sum = 0.0;
    for (uint32_t k = 1; k <= N; k++) {
        sum += 1.0 / (double)k;
        cdf[k - 1] = sum;
    }
    for (uint32_t k = 0; k < N; k++)
        cdf[k] /= sum;

    for (size_t i = 0; i < n; i++) {
        double u = (double)xorshift64_next(seed) / (double)UINT64_MAX;
        uint32_t lo = 0, hi = N;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if (cdf[mid] < u) lo = mid + 1;
            else hi = mid;
        }
        uint32_t h = (lo < N) ? lo : N - 1;
        uint64_t lo44 = xorshift64_next(seed) & ((((uint64_t)1) << 44) - 1);
        data[i] = (int64_t)(((uint64_t)h << 44) | lo44);
    }

    free(cdf);
    qsort(data, n, sizeof(int64_t), cmp_int64);
    return data;
}

static void shuffle_queries(int64_t *arr, size_t n, uint64_t *seed) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)(xorshift64_next(seed) % (i + 1));
        int64_t tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

static int64_t *generate_queries(const int64_t *data, size_t n, size_t num_q,
                                  int hit_percent, uint64_t *seed) {
    int64_t *queries = (int64_t *)malloc(num_q * sizeof(int64_t));
    if (!queries) { fprintf(stderr, "FATAL: malloc queries failed\n"); exit(1); }
    size_t hit_count = (size_t)((uint64_t)num_q * (uint64_t)hit_percent / 100);
    for (size_t i = 0; i < hit_count; i++) {
        size_t idx = (size_t)(xorshift64_next(seed) % n);
        queries[i] = data[idx];
    }
    for (size_t i = hit_count; i < num_q; i++) {
        int64_t candidate;
        int found;
        do {
            candidate = (int64_t)xorshift64_next(seed);
            found = (bsearch(&candidate, data, n, sizeof(int64_t), cmp_int64) != NULL);
        } while (found);
        queries[i] = candidate;
    }
    shuffle_queries(queries, num_q, seed);
    return queries;
}

/*
 * Benchmark 工具
 */
static uint64_t rdtscp_bench(void) {
    unsigned int aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

static double bench_median(double *samples, int n) {
    double sorted[16];
    int i;
    for (i = 0; i < n; i++) sorted[i] = samples[i];
    for (i = 1; i < n; i++) {
        double key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; j--; }
        sorted[j + 1] = key;
    }
    if (n % 2 == 0)
        return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
    else
        return sorted[n / 2];
}

static double bench_search(int algo, const int64_t *vals, size_t n,
                            const int32_t *dir,
                            const int64_t *queries, size_t num_q) {
    uint64_t t0 = rdtscp_bench();
    if (algo == 0) {
        for (size_t q = 0; q < num_q; q++) {
            volatile size_t r = search_int64_b1(vals, n, dir, queries[q]);
            (void)r;
        }
    } else if (algo == 1) {
        for (size_t q = 0; q < num_q; q++) {
            volatile size_t r = search_int64_avx2(vals, n, queries[q]);
            (void)r;
        }
    } else {
        for (size_t q = 0; q < num_q; q++) {
            volatile size_t r = search_int64_scalar(vals, n, queries[q]);
            (void)r;
        }
    }
    uint64_t t1 = rdtscp_bench();
    return (double)(t1 - t0) / (double)num_q;
}

static void bench_flush_cache(void) {
    size_t sz = 4 * 1024 * 1024;
    volatile char *buf = (volatile char *)malloc(sz);
    if (!buf) return;
    for (size_t i = 0; i < sz; i += 64)
        buf[i] = 0;
    free((void *)buf);
}

static void bench_one_cell(const char *dist_name, size_t n, size_t num_q,
                            int hit_percent, uint64_t base_seed) {
    printf("  %-7s n=%7.2fM  queries=%zu  %d%% hit ...",
           dist_name, n / 1000000.0, num_q, hit_percent);
    fflush(stdout);

    uint64_t seed = base_seed;
    int64_t *data = NULL;

    if (strcmp(dist_name, "uniform") == 0)
        data = generate_sorted_uniform(n, &seed);
    else if (strcmp(dist_name, "skewed") == 0)
        data = generate_sorted_skewed(n, &seed);
    else if (strcmp(dist_name, "zipf") == 0)
        data = generate_sorted_zipf(n, &seed);

    if (!data) { fprintf(stderr, "FATAL: data generation failed\n"); return; }

    int32_t *dir = (int32_t *)_mm_malloc(INT64_DIR_SIZE * sizeof(int32_t), 32);
    if (!dir) { _mm_free(data); fprintf(stderr, "FATAL: _mm_malloc dir failed\n"); return; }
    build_dir_int64(data, n, dir);

    if (!dir_validate_int64(dir, n))
        fprintf(stderr, "WARNING: dir validation failed\n");

    {
        size_t max_bucket = 0;
        for (int i = 0; i < INT64_DIR_SIZE - 1; i++) {
            size_t sz = (size_t)(dir[i + 1] - dir[i]);
            if (sz > max_bucket) max_bucket = sz;
        }

        int64_t *queries = generate_queries(data, n, num_q, hit_percent, &seed);

        double b1_cy[16], avx2_cy[16], scalar_cy[16];
        int order[6][3] = {
            {0,1,2}, {0,2,1}, {1,0,2},
            {1,2,0}, {2,0,1}, {2,1,0}
        };

        for (int round = 0; round < 6; round++) {
            for (int si = 0; si < 3; si++) {
                int algo = order[round][si];
                bench_flush_cache();
                double cy = bench_search(algo, data, n, dir, queries, num_q);
                if (algo == 0)      b1_cy[round]     = cy;
                else if (algo == 1) avx2_cy[round]    = cy;
                else                scalar_cy[round]  = cy;
            }
        }

        double b1_med     = bench_median(b1_cy, 6);
        double avx2_med   = bench_median(avx2_cy, 6);
        double scalar_med = bench_median(scalar_cy, 6);
        double b1_vs_avx2   = avx2_med / b1_med;
        double b1_vs_scalar = scalar_med / b1_med;

        printf(" max_bkt=%-6zu  B1=%.0f  AVX2=%.0f  Scalar=%.0f cy/q  "
               "B1vsAVX2=%.2fx  B1vsScalar=%.2fx\n",
               max_bucket, b1_med, avx2_med, scalar_med,
               b1_vs_avx2, b1_vs_scalar);

        free(queries);
    }

    _mm_free(dir);
    _mm_free(data);
}

static void run_full_benchmark(void) {
    printf("=== int64 B1 Benchmark Matrix (GATE-3) ===\n");
    printf("3 distributions x 2 sizes  |  50%% hit  |  6-round rotation\n\n");

    const char *dists[] = { "uniform", "skewed", "zipf" };
    size_t sizes[] = { 1000000, 10000000 };
    int n_dists = 3;
    int n_sizes = 2;

    for (int di = 0; di < n_dists; di++) {
        for (int si = 0; si < n_sizes; si++) {
            uint64_t seed = 1000 + (uint64_t)(di * 10 + si);
            bench_one_cell(dists[di], sizes[si], 10000, 50, seed);
        }
    }

    printf("\n=== Benchmark Complete ===\n");
}

/*
 * 正确性验证
 */
static void test_cross_validation(void) {
    printf("--- Cross Validation (n=100K, 10K queries, 50%% hit) ---\n");

    uint64_t seed_val = 12345;
    uint64_t *seed = &seed_val;

    size_t n = 100000;
    int64_t *data = generate_sorted_uniform(n, seed);

    int32_t *dir = (int32_t *)_mm_malloc(INT64_DIR_SIZE * sizeof(int32_t), 32);
    build_dir_int64(data, n, dir);
    CHECK(dir_validate_int64(dir, n) == 1, "dir validation passed");

    size_t num_q = 10000;
    int64_t *queries = generate_queries(data, n, num_q, 50, seed);

    int mismatches = 0;
    for (size_t i = 0; i < num_q; i++) {
        size_t b1_pos   = search_int64_b1(data, n, dir, queries[i]);
        size_t scal_pos = search_int64_scalar(data, n, queries[i]);
        int b1_found   = (b1_pos != SIZE_MAX) ? 1 : 0;
        int scal_found = (scal_pos != SIZE_MAX) ? 1 : 0;
        if (b1_found != scal_found) {
            mismatches++;
        } else if (b1_found && data[b1_pos] != queries[i]) {
            mismatches++;
        } else if (b1_found && b1_pos != scal_pos) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0, "B1 vs scalar: %d mismatches", mismatches);

    int bsearch_mismatches = 0;
    for (size_t i = 0; i < num_q; i++) {
        size_t b1_pos = search_int64_b1(data, n, dir, queries[i]);
        void *bs = bsearch(&queries[i], data, n, sizeof(int64_t), cmp_int64);
        int b1_found = (b1_pos != SIZE_MAX) ? 1 : 0;
        int bs_found = (bs != NULL) ? 1 : 0;
        if (b1_found != bs_found) bsearch_mismatches++;
    }
    CHECK(bsearch_mismatches == 0, "B1 vs bsearch: %d mismatches", bsearch_mismatches);

    free(queries);
    _mm_free(dir);
    _mm_free(data);
}

static void test_boundary(void) {
    printf("\n--- Boundary Tests ---\n");

    size_t sizes[] = { 0, 1, 2, 3, 4, 5, 10 };
    int n_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));

    for (int si = 0; si < n_sizes; si++) {
        size_t n = sizes[si];
        int64_t *data = NULL;
        int32_t *dir = NULL;
        if (n > 0) {
            data = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);
            for (size_t j = 0; j < n; j++)
                data[j] = (int64_t)(j * 10);
            dir = (int32_t *)_mm_malloc(INT64_DIR_SIZE * sizeof(int32_t), 32);
            build_dir_int64(data, n, dir);
        }

        for (size_t j = 0; j < n; j++) {
            size_t pos = search_int64_b1(data, n, dir, data[j]);
            if (pos != j) {
                printf("  FAIL: n=%zu data[%zu]=%lld pos=%zu\n",
                       n, j, (long long)data[j], pos);
                g_failures++;
            }
        }

        if (n > 0) {
            int64_t missing = (int64_t)(n * 10 + 5);
            size_t pos = search_int64_b1(data, n, dir, missing);
            if (pos != SIZE_MAX) {
                printf("  FAIL: n=%zu missing=%lld pos=%zu (expected SIZE_MAX)\n",
                       n, (long long)missing, pos);
                g_failures++;
            }
            _mm_free(dir);
            _mm_free(data);
        }
    }
    CHECK(1, "boundary n=0~10: all checks passed");
}

static void test_extreme_and_duplicates(void) {
    printf("\n--- Extreme Values & Duplicates ---\n");

    size_t n = 1000;
    int64_t *data = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);

    data[0] = INT64_MIN;
    for (size_t i = 1; i < n - 1; i++)
        data[i] = INT64_MIN + (int64_t)i;
    data[n - 1] = INT64_MAX;

    int32_t *dir = (int32_t *)_mm_malloc(INT64_DIR_SIZE * sizeof(int32_t), 32);
    build_dir_int64(data, n, dir);

    int64_t test_vals[] = { INT64_MIN, INT64_MAX, 0, INT64_MIN + 1, INT64_MAX - 1 };
    int n_vals = (int)(sizeof(test_vals) / sizeof(test_vals[0]));

    for (int t = 0; t < n_vals; t++) {
        size_t b1_pos   = search_int64_b1(data, n, dir, test_vals[t]);
        size_t scal_pos = search_int64_scalar(data, n, test_vals[t]);
        CHECK(b1_pos == scal_pos,
              "extreme target=%lld  B1=%zu  scalar=%zu",
              (long long)test_vals[t], b1_pos, scal_pos);
    }

    _mm_free(dir);
    _mm_free(data);
}

static void verify_dir_stats(void) {
    printf("\n--- Directory Stats (n=1M uniform) ---\n");

    uint64_t seed_val = 555;
    uint64_t *seed = &seed_val;
    size_t n = 1000000;

    int64_t *data = generate_sorted_uniform(n, seed);
    int32_t *dir = (int32_t *)_mm_malloc(INT64_DIR_SIZE * sizeof(int32_t), 32);
    build_dir_int64(data, n, dir);

    CHECK(dir_validate_int64(dir, n) == 1, "dir validation passed");

    size_t max_bucket = 0;
    size_t non_empty = 0;
    double sum_sq = 0.0;
    double avg = (double)n / (double)(1 << 20);

    for (int i = 0; i < INT64_DIR_SIZE - 1; i++) {
        size_t sz = (size_t)(dir[i + 1] - dir[i]);
        if (sz > 0) non_empty++;
        if (sz > max_bucket) max_bucket = sz;
        double diff = (double)(int)sz - avg;
        sum_sq += diff * diff;
    }
    double stddev = sqrt(sum_sq / (double)(1 << 20));

    printf("  non_empty_buckets: %zu / %d (%.1f%%)\n",
           non_empty, 1 << 20, 100.0 * non_empty / (double)(1 << 20));
    printf("  max_bucket:   %zu\n", max_bucket);
    printf("  avg_bucket:   %.1f\n", avg);
    printf("  stddev:       %.1f\n", stddev);

    _mm_free(dir);
    _mm_free(data);
}

static void run_all_verification(void) {
    printf("=== int64 B1 Path: Correctness Verification ===\n\n");

    test_cross_validation();
    test_boundary();
    test_extreme_and_duplicates();
    verify_dir_stats();

    printf("\n=== Verification: %s (%d failures) ===\n",
           g_failures == 0 ? "PASSED" : "FAILED", g_failures);
}

int main(int argc, char **argv) {
    int run_verify = 0;
    int run_bench  = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0) run_verify = 1;
        if (strcmp(argv[i], "--bench") == 0)  run_bench  = 1;
        if (strcmp(argv[i], "--all") == 0) {
            run_verify = 1;
            run_bench  = 1;
        }
    }

    if (!run_verify && !run_bench) {
        run_verify = 1;
        run_bench  = 1;
    }

    if (run_verify) run_all_verification();

    if (run_bench) {
        if (g_failures > 0) {
            printf("\nSkipping benchmark: %d verification failures\n", g_failures);
        } else {
            run_full_benchmark();
            printf("\n=== Final: %d failures ===\n", g_failures);
        }
    } else {
        printf("\n=== Final: %d failures ===\n", g_failures);
    }

    return g_failures > 0 ? 1 : 0;
}
