/*
 * poc_b1_crossover.c — D-015 散点采集: B1 crossover 分析 (Step 3)
 *
 * 编译命令:
 *
 *   :: 性能测量用 (O3)
 *   gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
 *       src/poc_b1_crossover.c -o poc_b1_crossover -lm
 *
 *   :: ASan/UBSan 验证用 (Linux/Mac)
 *   gcc -O1 -g -std=c11 -mavx2 -march=native -fsanitize=address,undefined \
 *       -fno-omit-frame-pointer src/poc_b1_crossover.c -o poc_b1_crossover_asan -lm
 *
 *   :: Windows MinGW (不含 sanitizer)
 *   gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
 *       src/poc_b1_crossover.c -o poc_b1_crossover.exe -lm
 *
 * 运行:
 *   poc_b1_crossover --verify      :: 受控构造正确性验证 (CROSS-01)
 *   poc_b1_crossover --bench       :: B 级受控构造散点 benchmark (CROSS-01, D-074)
 *   poc_b1_crossover --natural     :: A 级自然分布验证 (CROSS-02, D-074)
 *                                    5 scales × uniform+skewed
 *
 * B 级受控构造 (D-074):
 *   M ∈ {1,2,5,10,20,50,100,200,500,1K,2K,5K,10K,20K,50K,100K,n/2,n}
 *   每个 M 值生成排序数组，保证 max_bucket == M
 *
 * A 级自然分布验证 (D-074):
 *   5 规模 {100K,500K,1.5M,5M,10M} × uniform+skewed distribution
 *   每组合 1 个验证点，交叉验证 max_bucket 自然出现值
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

static int32_t *generate_sorted_data(size_t n, unsigned int *seed) {
    int32_t *data = (int32_t *)_mm_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)(lcg_next(seed) ^ (lcg_next(seed) << 11));
    qsort(data, n, sizeof(int32_t), compare_int32);
    return data;
}

static int32_t *generate_controlled_data(size_t n, size_t max_bucket, size_t *actual_n) {
    size_t eff_n;
    if (max_bucket > 65536) {
        eff_n = n;
    } else {
        size_t max_possible = max_bucket * 32768ull;
        eff_n = (n < max_possible) ? n : max_possible;
    }
    if (actual_n) *actual_n = eff_n;
    int32_t *data = (int32_t *)_mm_malloc(eff_n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    if (max_bucket > 65536) {
        for (size_t i = 0; i < eff_n; i++)
            data[i] = (int32_t)((uint32_t)i);
    } else {
        for (size_t i = 0; i < eff_n; i++) {
            uint16_t hi = (uint16_t)(i / max_bucket);
            uint16_t lo = (uint16_t)(i % max_bucket);
            data[i] = (int32_t)(((uint32_t)hi << 16) | (uint32_t)lo);
        }
    }
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

static size_t dir_max_bucket(const int32_t *dir) {
    size_t max_sz = 0;
    for (int i = 0; i < 65536; i++) {
        int32_t sz = dir[i + 1] - dir[i];
        if (sz > 0 && (size_t)sz > max_sz) max_sz = (size_t)sz;
    }
    return max_sz;
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

static void bench_crossover_one(size_t n, size_t max_bucket, FILE *csv) {
    unsigned int seed = 12345 + (unsigned int)max_bucket;

    size_t eff_n = 0;
    int32_t *data = generate_controlled_data(n, max_bucket, &eff_n);

    printf("  n=%.2fM  M=%-8zu ...", eff_n / 1000000.0, max_bucket);
    fflush(stdout);

    uint8_t *pool = b1_pool_build(data, eff_n);
    if (!pool) {
        fprintf(stderr, "\nFATAL: b1_pool_build failed n=%zu M=%zu\n", eff_n, max_bucket);
        _mm_free(data); return;
    }

    const int32_t *pool_dir = B1_POOL_DIR(pool);

    int32_t  *dir3 = (int32_t *)_mm_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    uint16_t *lo16 = (uint16_t *)_mm_malloc(eff_n * sizeof(uint16_t), 32);
    if (!dir3 || !lo16) {
        fprintf(stderr, "\nFATAL: _mm_malloc failed in bench_crossover_one n=%zu M=%zu\n",
                eff_n, max_bucket);
        if (dir3) _mm_free(dir3);
        if (lo16) _mm_free(lo16);
        b1_pool_destroy(pool);
        _mm_free(data);
        return;
    }
    high16_dir_build(data, eff_n, dir3);
    build_lo16(data, eff_n, lo16);

    size_t actual_max = dir_max_bucket(pool_dir);

    size_t num_q = (eff_n < 100000) ? eff_n : 100000;
    int32_t *queries = generate_queries(data, eff_n, num_q, 50, &seed);

    bench_context_t ctx;
    ctx.vals = data;
    ctx.n    = eff_n;
    ctx.dir  = dir3;
    ctx.lo16 = lo16;
    ctx.pool = pool;

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

    if (csv) {
        fprintf(csv, "%zu,%zu,%.1f,%.1f,%.1f,%.2f,%.2f\n",
                eff_n, actual_max,
                pool_f.median, ptr3_f.median, scalar_f.median,
                speedup_pool, speedup_ptr3);
        fflush(csv);
    }

    printf(" pool=%.1f  3ptr=%.1f  scalar=%.1f  cy/q  actual_max=%zu\n",
           pool_f.median, ptr3_f.median, scalar_f.median, actual_max);

    free(queries);
    _mm_free(lo16);
    _mm_free(dir3);
    b1_pool_destroy(pool);
    _mm_free(data);
}

static void run_crossover_bench_b_level(size_t n) {
    printf("=== B1 Crossover: B 级受控构造 (CROSS-01, D-074) ===\n");
    printf("n=%.2fM  queries=100K  50%% hit  6-round rotation\n\n", n / 1000000.0);

    FILE *csv = fopen("bench_b1_crossover.csv", "w");
    if (csv) {
        fprintf(csv, "n,max_bucket,b1_pool_cy_q,b1_3ptr_cy_q,scalar_cy_q,pool_vs_scalar,3ptr_vs_scalar\n");
    }

    size_t m_absolute[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000,
                           10000, 20000, 50000, 100000};
    int m_abs_n = (int)(sizeof(m_absolute) / sizeof(m_absolute[0]));

    for (int i = 0; i < m_abs_n; i++) {
        if (m_absolute[i] <= n)
            bench_crossover_one(n, m_absolute[i], csv);
    }

    bench_crossover_one(n, n / 2, csv);
    bench_crossover_one(n, n,     csv);

    if (csv) fclose(csv);
    printf("\n=== Crossover Benchmark Complete ===\n");
    printf("Results saved to: bench_b1_crossover.csv\n");
}

static void bench_natural_one(size_t n, int is_skewed, FILE *csv) {
    unsigned int seed = 12345;
    const char *dist_name = is_skewed ? "skewed" : "uniform";

    size_t num_q;
    if (n <= 100000)      num_q = (n < 200000) ? 2 * n : 200000;
    else if (n <= 500000) num_q = 500000;
    else                  num_q = 1000000;

    printf("\n  n=%.2fM  dist=%-7s  queries=%.1fM ...",
           n / 1000000.0, dist_name, num_q / 1000000.0);
    fflush(stdout);

    int32_t *data;
    if (is_skewed)
        data = generate_sorted_skewed(n, &seed);
    else
        data = generate_sorted_data(n, &seed);

    uint8_t *pool = b1_pool_build(data, n);
    if (!pool) {
        fprintf(stderr, "\nFATAL: b1_pool_build failed n=%zu dist=%s\n", n, dist_name);
        _mm_free(data); return;
    }

    const int32_t *pool_dir = B1_POOL_DIR(pool);

    int32_t  *dir3 = (int32_t *)_mm_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    uint16_t *lo16 = (uint16_t *)_mm_malloc(n * sizeof(uint16_t), 32);
    if (!dir3 || !lo16) {
        fprintf(stderr, "\nFATAL: _mm_malloc failed in bench_natural_one n=%zu dist=%s\n",
                n, dist_name);
        if (dir3) _mm_free(dir3);
        if (lo16) _mm_free(lo16);
        b1_pool_destroy(pool);
        _mm_free(data);
        return;
    }
    high16_dir_build(data, n, dir3);
    build_lo16(data, n, lo16);

    size_t max_bucket = dir_max_bucket(pool_dir);

    int32_t *queries = generate_queries(data, n, num_q, 50, &seed);

    bench_context_t ctx;
    ctx.vals = data;
    ctx.n    = n;
    ctx.dir  = dir3;
    ctx.lo16 = lo16;
    ctx.pool = pool;

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

    if (csv) {
        fprintf(csv, "%zu,%s,%zu,%.1f,%.1f,%.1f,%.2f,%.2f\n",
                n, dist_name, max_bucket,
                pool_f.median, ptr3_f.median, scalar_f.median,
                speedup_pool, speedup_ptr3);
        fflush(csv);
    }

    printf(" max_bkt=%-6zu  pool=%.1f  3ptr=%.1f  scalar=%.1f  cy/q  "
           "pool=%.1fx  3ptr=%.1fx\n",
           max_bucket, pool_f.median, ptr3_f.median, scalar_f.median,
           speedup_pool, speedup_ptr3);

    free(queries);
    _mm_free(lo16);
    _mm_free(dir3);
    b1_pool_destroy(pool);
    _mm_free(data);
}

static void run_crossover_bench_a_level(void) {
    printf("=== B1 Crossover: A 级自然分布验证 (CROSS-02, D-074) ===\n");
    printf("5 scales x 2 distributions  50%% hit  6-round rotation\n\n");

    FILE *csv = fopen("bench_b1_crossover_natural.csv", "w");
    if (csv) {
        fprintf(csv, "n,dist,max_bucket,b1_pool_cy_q,b1_3ptr_cy_q,"
                "scalar_cy_q,pool_vs_scalar,3ptr_vs_scalar\n");
    }

    size_t sizes[] = {100000, 500000, 1500000, 5000000, 10000000};
    int num_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));

    for (int si = 0; si < num_sizes; si++) {
        bench_natural_one(sizes[si], 0, csv);
        bench_natural_one(sizes[si], 1, csv);
    }

    if (csv) fclose(csv);
    printf("\n=== Natural Distribution Benchmark Complete ===\n");
    printf("Results saved to: bench_b1_crossover_natural.csv\n");
}

static void verify_controlled_construction(void) {
    printf("--- [CROSS-01] 受控构造正确性验证 ---\n");

    size_t n_ref = 10000000;
    size_t test_M[] = {1, 10, 100, 1000, 10000, 100000, n_ref / 2, n_ref};
    int num_tests = (int)(sizeof(test_M) / sizeof(test_M[0]));

    for (int ti = 0; ti < num_tests; ti++) {
        size_t M = test_M[ti];
        size_t eff_n = 0;
        int32_t *data = generate_controlled_data(n_ref, M, &eff_n);
        printf("\n  M=%-8zu  effective_n=%zu ...\n", M, eff_n);

        int32_t *dir = (int32_t *)_mm_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
        if (!dir) {
            fprintf(stderr, "FATAL: _mm_malloc(dir) failed for M=%zu\n", M);
            _mm_free(data);
            continue;
        }
        high16_dir_build(data, eff_n, dir);

        CHECK(dir_validate(dir, eff_n) == 1, "dir_validate passed for M=%zu", M);

        size_t max_bkt = dir_max_bucket(dir);
        if (M > 65536)
            CHECK(max_bkt >= 65535, "max_bucket=%zu >= 65535 for M=%zu (B1 hi16 limit)", max_bkt, M);
        else
            CHECK(max_bkt == M, "max_bucket=%zu == expected M=%zu", max_bkt, M);

        CHECK(data[0] == 0, "data[0] == 0 (M=%zu)", M);
        {
            size_t last = eff_n - 1;
            int32_t expected;
            if (M > 65536)
                expected = (int32_t)((uint32_t)last);
            else {
                uint16_t hi_exp = (uint16_t)(last / M);
                uint16_t lo_exp = (uint16_t)(last % M);
                expected = (int32_t)(((uint32_t)hi_exp << 16) | (uint32_t)lo_exp);
            }
            CHECK(data[last] == expected,
                  "data[n-1] correct (M=%zu)", M);
        }

        {
            int monotonic = 1;
            size_t fail_i = 0;
            for (size_t i = 1; i < eff_n; i++) {
                if (data[i] <= data[i-1]) {
                    monotonic = 0;
                    fail_i = i;
                    break;
                }
            }
            CHECK(monotonic, "monotonic (%zu elements), first violation at i=%zu (M=%zu)",
                  eff_n, fail_i, M);
        }

        uint16_t *lo16 = (uint16_t *)_mm_malloc(eff_n * sizeof(uint16_t), 32);
        if (!lo16) {
            fprintf(stderr, "FATAL: _mm_malloc(lo16) failed for M=%zu\n", M);
            _mm_free(dir);
            _mm_free(data);
            continue;
        }
        build_lo16(data, eff_n, lo16);

        unsigned int seed = 42 + (unsigned int)M;
        size_t num_q = (eff_n < 10000) ? eff_n : 10000;
        int32_t *queries = generate_queries(data, eff_n, num_q, 50, &seed);

        int mismatches = 0;
        for (size_t q = 0; q < num_q; q++) {
            int32_t b1_pos  = search_b1_3ptr(data, lo16, dir, eff_n, queries[q]);
            int32_t sc_pos  = search_scalar_bs(data, eff_n, queries[q]);
            int b1_found = (b1_pos >= 0) ? 1 : 0;
            int sc_found = (sc_pos >= 0) ? 1 : 0;
            if (b1_found != sc_found) mismatches++;
        }
        CHECK(mismatches == 0, "B1 3-ptr vs scalar: %d mismatches (M=%zu)", mismatches, M);

        uint8_t *pool = b1_pool_build(data, eff_n);
        CHECK(pool != NULL, "b1_pool_build success (M=%zu)", M);
        if (pool) {
            mismatches = 0;
            for (size_t q = 0; q < num_q; q++) {
                int32_t pool_pos = search_b1_pool(pool, data, eff_n, queries[q]);
                int32_t sc_pos   = search_scalar_bs(data, eff_n, queries[q]);
                int pool_found = (pool_pos >= 0) ? 1 : 0;
                int sc_found   = (sc_pos >= 0) ? 1 : 0;
                if (pool_found != sc_found) mismatches++;
            }
            CHECK(mismatches == 0, "B1 pool vs scalar: %d mismatches (M=%zu)", mismatches, M);
            b1_pool_destroy(pool);
        }

        free(queries);
        _mm_free(lo16);
        _mm_free(dir);
        _mm_free(data);
    }
}

static void verify_boundary_keys(void) {
    printf("\n--- [CROSS-01] 边界 key 验证 (M=100) ---\n");

    size_t n = 10000000;
    size_t M = 100;
    size_t eff_n = 0;
    int32_t *data = generate_controlled_data(n, M, &eff_n);
    printf("  effective_n=%zu\n", eff_n);

    int32_t boundary_keys[] = { INT32_MIN, -1, 0, 1, INT32_MAX };
    const char *boundary_names[] = { "INT32_MIN", "-1", "0", "1", "INT32_MAX" };
    int num_keys = (int)(sizeof(boundary_keys) / sizeof(boundary_keys[0]));

    for (int k = 0; k < num_keys; k++) {
        int32_t target = boundary_keys[k];
        int32_t b1_pos = search_scalar_bs(data, eff_n, target);
        int bs_found = (bsearch(&target, data, eff_n, sizeof(int32_t), compare_int32) != NULL);
        if (b1_pos >= 0)
            CHECK(data[b1_pos] == target,
                  "boundary %s: scalar found at %d (val=%d == target=%d)",
                  boundary_names[k], b1_pos, data[b1_pos], target);
        else
            CHECK(bs_found == 0, "boundary %s: scalar -1, bsearch -1", boundary_names[k]);
    }

    _mm_free(data);
}

int main(int argc, char **argv) {
    int bench = 0;
    int natural = 0;
    int verify = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bench") == 0)   bench = 1;
        if (strcmp(argv[i], "--natural") == 0) natural = 1;
        if (strcmp(argv[i], "--verify") == 0)  verify = 1;
    }

    if (!bench && !natural && !verify) {
        verify = 1;
    }

    if (verify) {
        printf("=== B1 Crossover: Correctness Verification (CROSS-01) ===\n\n");
        verify_controlled_construction();
        verify_boundary_keys();
        printf("\n=== %s: %d failures ===\n",
               g_failures == 0 ? "ALL PASSED" : "FAILURES",
               g_failures);
    }

    if (bench) {
        size_t n = 10000000;
        run_crossover_bench_b_level(n);
    }

    if (natural) {
        run_crossover_bench_a_level();
    }

    return g_failures > 0 ? 1 : 0;
}
