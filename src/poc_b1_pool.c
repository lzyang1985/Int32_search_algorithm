/*
 * poc_b1_pool.c — 内存池 B1 + 三指针 B1 + 标量三分对比 (Step 2)
 *
 * 编译命令:
 *
 *   :: 性能测量用 (O3)
 *   gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
 *       src/poc_b1_pool.c -o poc_b1_pool -lm
 *
 *   :: ASan/UBSan 验证用 (Linux/Mac)
 *   gcc -O1 -g -std=c11 -mavx2 -march=native -fsanitize=address,undefined \
 *       -fno-omit-frame-pointer src/poc_b1_pool.c -o poc_b1_pool_asan -lm
 *
 *   :: Windows MinGW (不含 sanitizer)
 *   gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
 *       src/poc_b1_pool.c -o poc_b1_pool.exe -lm
 *
 * 运行:
 *   poc_b1_pool                 :: 正确性验证 (POOL correct + 三指针 correct)
 *   poc_b1_pool --bench         :: 三分对比 benchmark (POOL-05, POOL-06, D-074)
 *
 * 内存池布局 (D-075):
 *   [0..262147]      dir   (65537 个 int32_t = 262148 bytes)
 *   [262148..262175] pad   (28 bytes, zero)
 *   [262176..]        lo16  (n 个 uint16_t = 2n bytes)
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

uint8_t *b1_pool_build(const int32_t *vals, size_t n) {
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

int32_t search_b1_pool(const uint8_t *pool, const int32_t *vals, size_t n, int32_t target) {
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

void b1_pool_destroy(uint8_t *pool) {
    _mm_free(pool);
}

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

typedef struct {
    const int32_t  *vals;
    size_t          n;
    const int32_t  *dir;
    const uint16_t *lo16;
    const uint8_t  *pool;
} bench_context_t;

static double bench_run_search(int algo, const bench_context_t *ctx,
                                const int32_t *queries, size_t num_q) {
    /* algo: 0=pool, 1=3ptr, 2=scalar */
    uint64_t t0 = rdtscp_bench();
    if (algo == 0) {
        for (size_t q = 0; q < num_q; q++) {
            volatile int32_t r = search_b1_pool(ctx->pool, ctx->vals, ctx->n, queries[q]);
            (void)r;
        }
    } else if (algo == 1) {
        for (size_t q = 0; q < num_q; q++) {
            volatile int32_t r = search_b1_3ptr(ctx->vals, ctx->lo16, ctx->dir, ctx->n, queries[q]);
            (void)r;
        }
    } else {
        for (size_t q = 0; q < num_q; q++) {
            volatile int32_t r = search_scalar_bs(ctx->vals, ctx->n, queries[q]);
            (void)r;
        }
    }
    uint64_t t1 = rdtscp_bench();
    return (double)(t1 - t0) / (double)num_q;
}

static bench_stats_t bench_round(int algo, const bench_context_t *ctx,
                                  const int32_t *queries, size_t num_q) {
    double samples[7];
    for (int rep = 0; rep < 7; rep++) {
        bench_flush_cache();
        samples[rep] = bench_run_search(algo, ctx, queries, num_q);
    }
    return bench_compute_stats(samples + 3, 4);
}

static void bench_three_way(const char *dist_name, size_t n, int is_skewed, FILE *csv) {
    unsigned int seed = 12345;

    int32_t *data;
    if (is_skewed)
        data = generate_sorted_skewed(n, &seed);
    else
        data = generate_sorted_data(n, &seed);

    uint8_t *pool = b1_pool_build(data, n);
    if (!pool) {
        fprintf(stderr, "FATAL: b1_pool_build failed n=%zu\n", n);
        _mm_free(data); return;
    }

    const int32_t  *pool_dir  = B1_POOL_DIR(pool);

    int32_t  *dir3 = (int32_t *)_mm_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    uint16_t *lo16 = (uint16_t *)_mm_malloc(n * sizeof(uint16_t), 32);
    high16_dir_build(data, n, dir3);
    build_lo16(data, n, lo16);

    size_t num_q = 1000000;
    int32_t *queries = generate_queries(data, n, num_q, 50, &seed);

    bench_context_t ctx;
    ctx.vals = data;
    ctx.n    = n;
    ctx.dir  = dir3;
    ctx.lo16 = lo16;
    ctx.pool = pool;

    int max_bucket = 0;
    for (int i = 0; i < 65536; i++) {
        int sz = pool_dir[i + 1] - pool_dir[i];
        if (sz > max_bucket) max_bucket = sz;
    }

    printf("\n=== n=%.2fM  dist=%-7s  max_bucket=%-6d ===\n",
           n / 1000000.0, dist_name, max_bucket);

    double pool_meds[6], ptr3_meds[6], scalar_meds[6];
    int order[6][3] = {
        {0,1,2}, {0,2,1}, {1,0,2},
        {1,2,0}, {2,0,1}, {2,1,0}
    };

    for (int round = 0; round < 6; round++) {
        for (int si = 0; si < 3; si++) {
            int algo = order[round][si];
            bench_stats_t s = bench_round(algo, &ctx, queries, num_q);
            if (algo == 0)      pool_meds[round]   = s.median;
            else if (algo == 1) ptr3_meds[round]   = s.median;
            else                scalar_meds[round]  = s.median;
        }
    }

    bench_stats_t pool_f   = bench_compute_stats(pool_meds,   6);
    bench_stats_t ptr3_f   = bench_compute_stats(ptr3_meds,   6);
    bench_stats_t scalar_f = bench_compute_stats(scalar_meds, 6);

    double speedup_pool = scalar_f.median / pool_f.median;
    double speedup_ptr3 = scalar_f.median / ptr3_f.median;

    printf("  B1 pool             med=%7.1f  min=%7.1f  max=%7.1f  σ=%.1f  cy/q\n",
           pool_f.median, pool_f.min, pool_f.max, pool_f.stddev);
    printf("  B1 3-ptr            med=%7.1f  min=%7.1f  max=%7.1f  σ=%.1f  cy/q\n",
           ptr3_f.median, ptr3_f.min, ptr3_f.max, ptr3_f.stddev);
    printf("  Scalar binary       med=%7.1f  min=%7.1f  max=%7.1f  σ=%.1f  cy/q\n",
           scalar_f.median, scalar_f.min, scalar_f.max, scalar_f.stddev);
    printf("  Pool vs Scalar      %.2fx\n", speedup_pool);
    printf("  3-ptr vs Scalar     %.2fx\n", speedup_ptr3);

    if (csv) {
        fprintf(csv, "%zu,%s,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f\n",
                n, dist_name, max_bucket,
                pool_f.median, ptr3_f.median, scalar_f.median,
                pool_f.stddev, ptr3_f.stddev, scalar_f.stddev,
                speedup_pool, speedup_ptr3);
    }

    free(queries);
    _mm_free(lo16);
    _mm_free(dir3);
    b1_pool_destroy(pool);
    _mm_free(data);
}

static void run_benchmark_all(void) {
    printf("=== B1 Pool POC: Three-way Benchmark (POOL-05, D-074) ===\n");
    printf("Method: 6-round rotation | 2MB cache flush | 7 repeats "
           "(discard 3, median 4) | 6 round-medians\n");
    printf("Queries: 1M per combo, 50%% hit\n");
    printf("Sizes: 1.5M / 5M / 10M\n");
    printf("Algorithms: B1 pool | B1 3-ptr | Scalar binary\n\n");

    FILE *csv = fopen("bench_b1_pool.csv", "w");
    if (csv) {
        fprintf(csv, "n,dist,max_bucket,pool_cy_q,ptr3_cy_q,scalar_cy_q,"
                "pool_stddev,ptr3_stddev,scalar_stddev,pool_vs_scalar,ptr3_vs_scalar\n");
    }

    size_t sizes[] = {1500000, 5000000, 10000000};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int si = 0; si < num_sizes; si++) {
        bench_three_way("uniform", sizes[si], 0, csv);
        bench_three_way("skewed",  sizes[si], 1, csv);
    }

    if (csv) fclose(csv);
    printf("\n=== Benchmark Complete ===\n");
    printf("Results saved to: bench_b1_pool.csv\n");
}

static void verify_pool_build(void) {
    printf("\n--- [POOL] Step 1: b1_pool_build 正确性 ---\n");
    unsigned int seed = 42;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);

    uint8_t *pool = b1_pool_build(data, n);
    CHECK(pool != NULL, "b1_pool_build(n=%zu) returned non-NULL", n);

    const int32_t  *dir  = B1_POOL_DIR(pool);
    const uint16_t *lo16 = B1_POOL_LO16(pool);

    int ok = dir_validate(dir, n);
    CHECK(ok == 1, "pool dir_validate returned %d", ok);

    for (size_t i = 0; i < n; i++) {
        if (lo16[i] != (uint16_t)(data[i] & 0xFFFF)) {
            CHECK(0, "pool lo16[%zu]=%u expected %u",
                  i, (unsigned)lo16[i], (unsigned)(data[i] & 0xFFFF));
            break;
        }
    }
    if (lo16[n-1] == (uint16_t)(data[n-1] & 0xFFFF)) {
        printf("  PASS: pool lo16[] matches vals[] for n=%zu\n", n);
    }

    b1_pool_destroy(pool);
    _mm_free(data);
}

static void verify_pool_hit100(void) {
    printf("\n--- [POOL] Step 2: pool 命中率 100%% (n=100K) ---\n");
    unsigned int seed = 99;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);
    uint8_t *pool = b1_pool_build(data, n);
    if (!pool) {
        CHECK(0, "b1_pool_build failed");
        _mm_free(data); return;
    }
    int32_t *queries = generate_queries(data, n, n, 100, &seed);
    int hits = 0;
    for (size_t q = 0; q < n; q++) {
        int32_t pos = search_b1_pool(pool, data, n, queries[q]);
        if (pos >= 0) hits++;
    }
    CHECK((size_t)hits == n, "100%% hit: hits=%d expected=%zu", hits, n);
    free(queries);
    b1_pool_destroy(pool);
    _mm_free(data);
}

static void verify_pool_hit0(void) {
    printf("\n--- [POOL] Step 3: pool 命中率 0%% (n=100K) ---\n");
    unsigned int seed = 77;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);
    uint8_t *pool = b1_pool_build(data, n);
    if (!pool) {
        CHECK(0, "b1_pool_build failed");
        _mm_free(data); return;
    }
    int32_t *queries = generate_queries(data, n, n, 0, &seed);
    int hits = 0;
    for (size_t q = 0; q < n; q++) {
        int32_t pos = search_b1_pool(pool, data, n, queries[q]);
        if (pos >= 0) hits++;
    }
    CHECK(hits == 0, "0%% hit: hits=%d expected=0", hits);
    free(queries);
    b1_pool_destroy(pool);
    _mm_free(data);
}

static void verify_pool_vs_3ptr(void) {
    printf("\n--- [POOL] Step 4: pool vs 3-ptr 交叉验证 (n=100K, 50%% hit) ---\n");
    unsigned int seed = 55;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);
    uint8_t *pool = b1_pool_build(data, n);
    if (!pool) {
        CHECK(0, "b1_pool_build failed");
        _mm_free(data); return;
    }

    int32_t  *dir3 = (int32_t *)_mm_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    uint16_t *lo16 = (uint16_t *)_mm_malloc(n * sizeof(uint16_t), 32);
    high16_dir_build(data, n, dir3);
    build_lo16(data, n, lo16);

    size_t num_q = n;
    int32_t *queries = generate_queries(data, n, num_q, 50, &seed);

    int mismatches = 0;
    int32_t first_mismatch_target = 0;
    size_t first_mismatch_idx = 0;
    for (size_t q = 0; q < num_q; q++) {
        int32_t pool_pos = search_b1_pool(pool, data, n, queries[q]);
        int32_t ptr3_pos = search_b1_3ptr(data, lo16, dir3, n, queries[q]);
        int pool_found = (pool_pos >= 0) ? 1 : 0;
        int ptr3_found = (ptr3_pos >= 0) ? 1 : 0;
        if (pool_found != ptr3_found) {
            if (mismatches == 0) {
                first_mismatch_target = queries[q];
                first_mismatch_idx = q;
            }
            mismatches++;
        }
    }
    CHECK(mismatches == 0,
          "pool vs 3-ptr: %d mismatches (first: target=%d idx=%zu)",
          mismatches, first_mismatch_target, first_mismatch_idx);

    mismatches = 0;
    for (size_t q = 0; q < num_q; q++) {
        int32_t pool_pos = search_b1_pool(pool, data, n, queries[q]);
        void *bs = bsearch(&queries[q], data, n, sizeof(int32_t), compare_int32);
        int pool_found = (pool_pos >= 0) ? 1 : 0;
        int bs_found = (bs != NULL) ? 1 : 0;
        if (pool_found != bs_found) {
            if (mismatches == 0) {
                first_mismatch_target = queries[q];
                first_mismatch_idx = q;
            }
            mismatches++;
        }
    }
    CHECK(mismatches == 0,
          "pool vs bsearch: %d mismatches (first: target=%d idx=%zu)",
          mismatches, first_mismatch_target, first_mismatch_idx);

    free(queries);
    _mm_free(lo16);
    _mm_free(dir3);
    b1_pool_destroy(pool);
    _mm_free(data);
}

static void verify_pool_boundary(void) {
    printf("\n--- [POOL] Step 5: pool 边界 key 验证 ---\n");
    unsigned int seed = 33;
    size_t n = 100000;
    int32_t *data = generate_sorted_data(n, &seed);
    uint8_t *pool = b1_pool_build(data, n);
    if (!pool) {
        CHECK(0, "b1_pool_build failed");
        _mm_free(data); return;
    }

    int32_t boundary_keys[] = { INT32_MIN, -1, 0, 1, INT32_MAX };
    const char *boundary_names[] = { "INT32_MIN", "-1", "0", "1", "INT32_MAX" };
    int num_keys = sizeof(boundary_keys) / sizeof(boundary_keys[0]);

    for (int k = 0; k < num_keys; k++) {
        int32_t target = boundary_keys[k];
        int32_t pool_pos = search_b1_pool(pool, data, n, target);
        int32_t sc_pos   = search_scalar_bs(data, n, target);
        if (pool_pos >= 0 && sc_pos >= 0) {
            CHECK(data[pool_pos] == target,
                  "boundary %s: pool found at %d (val=%d == target=%d)",
                  boundary_names[k], pool_pos, data[pool_pos], target);
        } else if (pool_pos < 0 && sc_pos < 0) {
            printf("  PASS: boundary %s: both pool and scalar return -1\n",
                   boundary_names[k]);
        } else {
            CHECK(0, "boundary %s: pool=%d scalar=%d MISMATCH",
                  boundary_names[k], pool_pos, sc_pos);
        }
    }

    b1_pool_destroy(pool);
    _mm_free(data);
}

int main(int argc, char **argv) {
    int bench = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bench") == 0) bench = 1;
    }

    if (bench) {
        run_benchmark_all();
        return 0;
    }

    printf("=== B1 Pool POC: Correctness Verification (POOL-01~04) ===\n\n");

    verify_pool_build();
    verify_pool_hit100();
    verify_pool_hit0();
    verify_pool_vs_3ptr();
    verify_pool_boundary();

    printf("\n=== %s: %d failures ===\n",
           g_failures == 0 ? "ALL PASSED" : "FAILURES",
           g_failures);
    return g_failures > 0 ? 1 : 0;
}
