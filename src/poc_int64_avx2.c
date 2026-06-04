/*
 * poc_int64_avx2.c — ACT-14: int64 Path A AVX2 4路SIMD二分验证
 *
 * 编译命令:
 *
 *   :: 性能测量用 (O3)
 *   gcc -O3 -std=c11 -mavx2 src/poc_int64_avx2.c -o poc_int64_avx2 -lm
 *
 *   :: ASan/UBSan 验证用 (Linux)
 *   gcc -O1 -g -std=c11 -mavx2 -fsanitize=address,undefined \
 *       -fno-omit-frame-pointer src/poc_int64_avx2.c -o poc_int64_avx2_asan -lm
 *
 *   :: Windows MinGW (不含 sanitizer)
 *   gcc -O3 -std=c11 -mavx2 src/poc_int64_avx2.c -o poc_int64_avx2.exe -lm
 *
 * 运行:
 *   poc_int64_avx2                :: 正确性验证 + 性能基准 (默认)
 *   poc_int64_avx2 --verify       :: 仅正确性验证 (GATE-1)
 *   poc_int64_avx2 --bench        :: 仅性能基准 (GATE-2)
 *   poc_int64_avx2 --all          :: 全部测试
 *   poc_int64_avx2_asan           :: ASan/UBSan 验证
 *
 * 验收标准:
 *   GATE-1: 10000 次交叉验证零差异 + 所有边界测试通过
 *   GATE-2: 10M 下 AVX2 加速比 >= 1.2x
 *   无 -fsanitize=address,undefined 告警
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

static int g_failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
        g_failures++; \
    } else { \
        printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define BENCH_REPS     7
#define BENCH_WARMUP   1
#define BENCH_TOTAL    (BENCH_WARMUP + BENCH_REPS)

static int cmp_int64(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

/*
 * xorshift64 伪随机数生成器
 */
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
 * 返回第一个匹配位置；未命中返回 SIZE_MAX
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
 * search_int64_avx2 — int64 4 路 SIMD 二分查找 (Path A, D-103 规范)
 * 返回第一个匹配位置；未命中返回 SIZE_MAX
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

static int64_t *generate_sorted_data(size_t n, uint64_t *seed) {
    int64_t *data = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);
    if (!data) { fprintf(stderr, "FATAL: _mm_malloc failed for n=%zu\n", n); exit(1); }
    for (size_t i = 0; i < n; i++)
        data[i] = (int64_t)xorshift64_next(seed);
    qsort(data, n, sizeof(int64_t), cmp_int64);
    return data;
}

static int64_t *generate_sorted_data_small(size_t n, int64_t fill_val) {
    int64_t *data = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);
    if (!data) { fprintf(stderr, "FATAL: _mm_malloc failed for n=%zu\n", n); exit(1); }
    for (size_t i = 0; i < n; i++)
        data[i] = fill_val + (int64_t)i;
    return data;
}

static int64_t *generate_sorted_duplicates(size_t n, uint64_t *seed) {
    int64_t *data = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);
    if (!data) { fprintf(stderr, "FATAL: _mm_malloc failed for n=%zu\n", n); exit(1); }
    size_t i = 0;
    while (i < n) {
        int64_t val = (int64_t)(xorshift64_next(seed) & 0x3FFFFFFFFFFFFFFFULL);
        int dup = (int)(xorshift64_next(seed) % 5) + 1;
        if (i + dup > n) dup = (int)(n - i);
        for (int d = 0; d < dup; d++)
            data[i + d] = val;
        i += (size_t)dup;
    }
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

static uint64_t rdtscp_bench(void) {
    unsigned int aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

static double bench_median(double *samples, int n) {
    double sorted[16];
    for (int i = 0; i < n; i++) sorted[i] = samples[i];
    for (int i = 1; i < n; i++) {
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

static double bench_search(const int64_t *vals, size_t n,
                            const int64_t *queries, size_t num_q,
                            int use_avx2) {
    uint64_t t0 = rdtscp_bench();
    if (use_avx2) {
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

static void run_benchmark(size_t n, size_t num_q, int hit_percent) {
    printf("\n=== Benchmark: n=%.2fM  queries=%zu  %d%% hit  %d reps ===\n\n",
           n / 1000000.0, num_q, hit_percent, BENCH_REPS);

    uint64_t seed_val = 42;
    uint64_t *seed = &seed_val;

    printf("  Generating sorted data (%.0f MB)...\n", (double)(n * 8) / 1048576.0);
    fflush(stdout);
    int64_t *data = generate_sorted_data(n, seed);

    printf("  Generating queries...\n");
    fflush(stdout);
    int64_t *queries = generate_queries(data, n, num_q, hit_percent, seed);

    double avx2_cy[16];
    double scalar_cy[16];

    printf("  Bench (AVX2 + Scalar)...\n");
    fflush(stdout);

    for (int rep = 0; rep < BENCH_TOTAL; rep++) {
        bench_flush_cache();
        avx2_cy[rep]   = bench_search(data, n, queries, num_q, 1);
        bench_flush_cache();
        scalar_cy[rep] = bench_search(data, n, queries, num_q, 0);
        printf("    rep %d:  AVX2=%.1f cy/q  Scalar=%.1f cy/q\n",
               rep + 1, avx2_cy[rep], scalar_cy[rep]);
    }

    double avx2_med   = bench_median(avx2_cy + BENCH_WARMUP, BENCH_REPS);
    double scalar_med = bench_median(scalar_cy + BENCH_WARMUP, BENCH_REPS);
    double speedup    = scalar_med / avx2_med;

    printf("\n  === Results (median of %d reps after warmup) ===\n", BENCH_REPS);
    printf("  AVX2:    %.1f cy/query\n", avx2_med);
    printf("  Scalar:  %.1f cy/query\n", scalar_med);
    printf("  Speedup: %.2fx\n", speedup);

    CHECK(speedup >= 1.2, "GATE-2: AVX2 speedup %.2fx >= 1.2x", speedup);

    free(queries);
    _mm_free(data);
}

static void test_cross_validation(void) {
    printf("--- Cross Validation (n=100K, 10000 queries, 50%% hit) ---\n");

    uint64_t seed_val = 12345;
    uint64_t *seed = &seed_val;

    size_t n = 100000;
    int64_t *data = generate_sorted_data(n, seed);

    size_t num_q = 10000;
    int64_t *queries = generate_queries(data, n, num_q, 50, seed);

    int mismatches = 0;
    for (size_t i = 0; i < num_q; i++) {
        size_t avx2_pos  = search_int64_avx2(data, n, queries[i]);
        size_t scal_pos  = search_int64_scalar(data, n, queries[i]);
        int avx2_found = (avx2_pos != SIZE_MAX) ? 1 : 0;
        int scal_found = (scal_pos != SIZE_MAX) ? 1 : 0;
        if (avx2_found != scal_found) {
            mismatches++;
        } else if (avx2_found && data[avx2_pos] != queries[i]) {
            mismatches++;
        } else if (avx2_found && avx2_pos != scal_pos) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0, "10000 random queries vs scalar: %d mismatches", mismatches);

    free(queries);
    _mm_free(data);
}

static void test_boundary_n(void) {
    printf("\n--- Boundary: n = 0, 1, 2, 3, 4 ---\n");

    for (size_t n = 0; n <= 4; n++) {
        int64_t *data = NULL;
        if (n > 0) {
            data = generate_sorted_data_small(n, 100);
        }

        int64_t targets[] = { 90, 100, 101, 103, 110, INT64_MIN, INT64_MAX, 0 };
        int num_targets = (int)(sizeof(targets) / sizeof(targets[0]));

        for (int t = 0; t < num_targets; t++) {
            int64_t target = targets[t];
            size_t avx2_pos = search_int64_avx2(data, n, target);
            size_t scal_pos = search_int64_scalar(data, n, target);

            int avx2_found = (avx2_pos != SIZE_MAX) ? 1 : 0;
            int scal_found = (scal_pos != SIZE_MAX) ? 1 : 0;

            if (avx2_found != scal_found) {
                printf("  FAIL: n=%zu target=%lld avx2=%zu scal=%zu\n",
                       n, (long long)target,
                       avx2_pos, scal_pos);
                g_failures++;
            }
            if (avx2_found && data[avx2_pos] != target) {
                printf("  FAIL: n=%zu target=%lld avx2_pos=%zu val=%lld != target\n",
                       n, (long long)target,
                       avx2_pos, (long long)data[avx2_pos]);
                g_failures++;
            }
            if (avx2_found && avx2_pos != scal_pos) {
                printf("  FAIL: n=%zu target=%lld avx2_pos=%zu != scal_pos=%zu\n",
                       n, (long long)target,
                       avx2_pos, scal_pos);
                g_failures++;
            }
        }

        if (n > 0) _mm_free(data);
    }
    CHECK(1, "n=0~4 boundary tests: no crash, no mismatch");
}

static void test_boundary_n_mixed(void) {
    printf("\n--- Boundary: n = 5, 7, 10, 15 (mixed SIMD + scalar) ---\n");

    size_t test_sizes[] = { 5, 7, 10, 15 };
    int num_sizes = (int)(sizeof(test_sizes) / sizeof(test_sizes[0]));

    for (int si = 0; si < num_sizes; si++) {
        size_t n = test_sizes[si];
        int64_t *data = generate_sorted_data_small(n, 0);

        for (size_t i = 0; i < n; i++) {
            int64_t target = data[i];
            size_t avx2_pos = search_int64_avx2(data, n, target);
            size_t scal_pos = search_int64_scalar(data, n, target);
            if (avx2_pos != i || scal_pos != i) {
                printf("  FAIL: n=%zu target=data[%zu]=%lld avx2_pos=%zu scal_pos=%zu\n",
                       n, i,
                       (long long)target,
                       avx2_pos, scal_pos);
                g_failures++;
            }
        }
        int64_t missing = data[n - 1] + 100;
        size_t avx2_pos = search_int64_avx2(data, n, missing);
        size_t scal_pos = search_int64_scalar(data, n, missing);
        if (avx2_pos != SIZE_MAX || scal_pos != SIZE_MAX) {
            printf("  FAIL: n=%zu missing=%lld avx2_pos=%zu scal_pos=%zu (expected SIZE_MAX)\n",
                   n, (long long)missing,
                   avx2_pos, scal_pos);
            g_failures++;
        }

        _mm_free(data);
    }
    CHECK(1, "n=5~15 mixed SIMD+scalar: all present keys found, missing key not found");
}

static void test_extreme_values(void) {
    printf("\n--- Extreme Values: INT64_MIN, INT64_MAX ---\n");

    size_t n = 1000;
    int64_t *data = generate_sorted_data_small(n, INT64_MIN);
    data[n - 1] = INT64_MAX;

    int64_t targets[] = { INT64_MIN, INT64_MAX, 0, (int64_t)((uint64_t)INT64_MIN + 1) };
    int num_targets = (int)(sizeof(targets) / sizeof(targets[0]));

    for (int t = 0; t < num_targets; t++) {
        int64_t target = targets[t];
        size_t avx2_pos = search_int64_avx2(data, n, target);
        size_t scal_pos = search_int64_scalar(data, n, target);
        CHECK(avx2_pos == scal_pos,
              "INT64_MIN/MAX/0: target=%lld  avx2=%zu  scal=%zu",
              (long long)target, avx2_pos, scal_pos);
    }

    _mm_free(data);
}

static void test_duplicates(void) {
    printf("\n--- Duplicate Elements ---\n");

    size_t n = 500;
    uint64_t seed_val = 999;
    uint64_t *seed = &seed_val;
    int64_t *data = generate_sorted_duplicates(n, seed);

    int64_t prev = data[0];
    int dup_count = 1;

    for (size_t i = 1; i < n; i++) {
        if (data[i] == prev) {
            dup_count++;
        } else {
            if (dup_count > 1) {
                size_t pos = search_int64_avx2(data, n, prev);
                CHECK(pos == i - (size_t)dup_count,
                      "dup key %lld: returns first occurrence pos=%zu (expected %zu)",
                      (long long)prev, pos,
                      i - (size_t)dup_count);
            }
            prev = data[i];
            dup_count = 1;
        }
    }
    if (dup_count > 1) {
        size_t pos = search_int64_avx2(data, n, prev);
        CHECK(pos == n - (size_t)dup_count,
              "last dup key %lld: returns first occurrence pos=%zu (expected %zu)",
              (long long)prev, pos,
              n - (size_t)dup_count);
    }

    int64_t bsearch_check = 1;
    for (size_t i = 0; i < n; i++) {
        int64_t target = data[i];
        size_t avx2_pos = search_int64_avx2(data, n, target);
        void *bs_result = bsearch(&target, data, n, sizeof(int64_t), cmp_int64);
        if (avx2_pos == SIZE_MAX || data[avx2_pos] != target) {
            bsearch_check = 0;
            printf("  FAIL: dup i=%zu avx2 miss for %lld\n",
                   i, (long long)target);
            g_failures++;
            break;
        }
        if (bs_result == NULL) {
            bsearch_check = 0;
            printf("  FAIL: dup i=%zu bsearch miss for %lld\n",
                   i, (long long)target);
            g_failures++;
            break;
        }
    }
    if (bsearch_check == 1)
        CHECK(1, "duplicates: all present keys found by both avx2 and bsearch");

    _mm_free(data);
}

static void test_bsearch_consistency(void) {
    printf("\n--- bsearch Consistency (n=100K, 10000 queries) ---\n");

    uint64_t seed_val = 54321;
    uint64_t *seed = &seed_val;

    size_t n = 100000;
    int64_t *data = generate_sorted_data(n, seed);

    size_t num_q = 10000;
    int64_t *queries = generate_queries(data, n, num_q, 50, seed);

    int mismatches = 0;
    for (size_t i = 0; i < num_q; i++) {
        size_t avx2_pos  = search_int64_avx2(data, n, queries[i]);
        void *bs_result   = bsearch(&queries[i], data, n, sizeof(int64_t), cmp_int64);
        int avx2_found = (avx2_pos != SIZE_MAX) ? 1 : 0;
        int bs_found   = (bs_result != NULL) ? 1 : 0;
        if (avx2_found != bs_found) {
            mismatches++;
        }
        if (avx2_found && data[avx2_pos] != queries[i]) {
            mismatches++;
        }
    }
    CHECK(mismatches == 0, "bsearch consistency: %d mismatches", mismatches);

    free(queries);
    _mm_free(data);
}

static void run_all_verification(void) {
    printf("=== int64 AVX2 Path A: Correctness Verification (GATE-1) ===\n\n");

    test_cross_validation();
    test_boundary_n();
    test_boundary_n_mixed();
    test_extreme_values();
    test_duplicates();
    test_bsearch_consistency();

    printf("\n=== GATE-1: %s (%d failures) ===\n",
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
            run_benchmark(10000000, 10000, 50);
        }
    }

    printf("\n=== Final: %d failures ===\n", g_failures);
    return g_failures > 0 ? 1 : 0;
}
