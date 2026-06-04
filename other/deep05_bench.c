#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>
#include <x86intrin.h>

static int compare_int32(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static int32_t *generate_sorted_data(size_t n)
{
    int32_t *data = (int32_t *)_mm_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)((uint32_t)rand() ^ ((uint32_t)rand() << 15));
    qsort(data, n, sizeof(int32_t), compare_int32);
    return data;
}

static int32_t *generate_queries(const int32_t *data, size_t n, size_t num_queries)
{
    int32_t *queries = (int32_t *)malloc(num_queries * sizeof(int32_t));
    if (!queries) { fprintf(stderr, "malloc failed\n"); exit(1); }
    size_t half = num_queries / 2;
    for (size_t i = 0; i < half; i++)
        queries[i] = data[(size_t)rand() % n];
    for (size_t i = half; i < num_queries; i++) {
        int32_t candidate;
        do {
            candidate = (int32_t)((uint32_t)rand() ^ ((uint32_t)rand() << 15));
        } while (bsearch(&candidate, data, n, sizeof(int32_t), compare_int32) != NULL);
        queries[i] = candidate;
    }
    for (size_t i = num_queries - 1; i > 0; i--) {
        size_t j = (size_t)rand() % (i + 1);
        int32_t tmp = queries[i];
        queries[i] = queries[j];
        queries[j] = tmp;
    }
    return queries;
}

static unsigned long long rdtscp_bench(void)
{
    unsigned int aux;
    unsigned long long t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

typedef struct {
    unsigned long long cycles;
    int simd_iters;
    int scalar_iters;
    int hits;
} query_stats_t;

static query_stats_t bench_phase1(const int32_t *vals, size_t n, const int32_t *queries, size_t num_queries)
{
    query_stats_t s = {0, 0, 0, 0};
    volatile int discard = 0;

    for (size_t i = 0; i < 100; i++)
        discard |= (vals[(size_t)rand() % n] > 0);

    unsigned long long t0 = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        int32_t target = queries[q];
        size_t lo = 0, hi = n;
        int simd_count = 0, scalar_count = 0;

        while (hi - lo >= 8) {
            simd_count++;
            size_t mid = lo + (hi - lo) / 2;
            size_t block = mid & ~(size_t)7;
            if (block < lo) block = lo;
            if (block + 8 > hi) block = hi - 8;
            __m256i key = _mm256_set1_epi32(target);
            __m256i vec = _mm256_loadu_si256((const __m256i *)(vals + block));
            __m256i cmp = _mm256_cmpgt_epi32(vec, key);
            int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
            int le_count = (int)(8u - (unsigned int)__builtin_popcount((unsigned int)mask));
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
            scalar_count++;
            size_t mid = lo + (hi - lo) / 2;
            if (vals[mid] < target) lo = mid + 1;
            else hi = mid;
        }
        if (lo < n && vals[lo] == target) s.hits++;
        s.simd_iters += simd_count;
        s.scalar_iters += scalar_count;
        discard |= (int)lo;
    }
    unsigned long long t1 = rdtscp_bench();
    (void)discard;

    s.cycles = t1 - t0;
    return s;
}

static query_stats_t bench_pocv3(const int32_t *arr, size_t n, const int32_t *queries, size_t num_queries)
{
    query_stats_t s = {0, 0, 0, 0};
    volatile int discard = 0;

    for (size_t i = 0; i < 100; i++)
        discard |= (arr[(size_t)rand() % n] > 0);

    unsigned long long t0 = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        int32_t target = queries[q];
        size_t lo = 0, hi = n;
        int simd_count = 0, scalar_count = 0;

        while (hi - lo >= 8) {
            simd_count++;
            size_t mid = lo + (hi - lo) / 2;
            size_t block = mid & ~(size_t)7;
            if (block + 8 > hi) block = hi - 8;
            __m256i k = _mm256_set1_epi32(target);
            __m256i v = _mm256_loadu_si256((const __m256i *)(arr + block));
            __m256i cmp = _mm256_cmpgt_epi32(k, v);
            int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
            int le_count = __builtin_popcount(mask ^ 0xFF);
            if (le_count == 8) {
                lo = block + 8;
            } else if (le_count == 0) {
                hi = block;
            } else {
                size_t idx = block + (size_t)le_count;
                if (arr[idx] == target) {
                    discard |= (int)idx;
                    s.hits++;
                    goto next_query;
                }
                lo = block + (size_t)le_count;
                hi = block + (size_t)le_count;
            }
        }
        while (lo < hi) {
            scalar_count++;
            size_t mid = lo + (hi - lo) / 2;
            if (arr[mid] < target) lo = mid + 1;
            else hi = mid;
        }
        if (lo < n && arr[lo] == target) s.hits++;
        next_query:
        s.simd_iters += simd_count;
        s.scalar_iters += scalar_count;
        discard |= (int)lo;
    }
    unsigned long long t1 = rdtscp_bench();
    (void)discard;

    s.cycles = t1 - t0;
    return s;
}

static double bench_scalar(const int32_t *vals, size_t n, const int32_t *queries, size_t num_queries, int *hits)
{
    volatile int discard = 0;

    for (size_t i = 0; i < 100; i++)
        discard |= (vals[(size_t)rand() % n] > 0);

    int h = 0;
    unsigned long long t0 = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        int32_t target = queries[q];
        size_t lo = 0, hi = n;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (vals[mid] < target) lo = mid + 1;
            else hi = mid;
        }
        if (lo < n && vals[lo] == target) h++;
        discard |= (int)lo;
    }
    unsigned long long t1 = rdtscp_bench();
    (void)discard;

    *hits = h;
    return (double)(t1 - t0) / (double)num_queries;
}

int main(void)
{
    size_t sizes[] = {1000000, 10000000};
    int n_sizes = 2;

    printf("DEEP-05: Phase 1 vs POC v3 Algorithmic Analysis\n");
    printf("================================================\n");
    printf("CPU: Xeon Gold 6152 @ 2.10GHz (16 cores)\n");
    printf("Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    printf("\n");

    for (int si = 0; si < n_sizes; si++) {
        size_t n = sizes[si];
        size_t num_queries = 100000;

        srand(12345);
        int32_t *data = generate_sorted_data(n);
        int32_t *queries = generate_queries(data, n, num_queries);

        printf("--- N = %zu (%.1fM) ---\n", n, n / 1000000.0);

        int scalar_hits;
        double scalar_cy = bench_scalar(data, n, queries, num_queries, &scalar_hits);
        printf("  Scalar binsearch:        %8.1f cy/q  (hits=%d)\n", scalar_cy, scalar_hits);

        query_stats_t s1 = bench_phase1(data, n, queries, num_queries);
        double cy1 = (double)s1.cycles / (double)num_queries;
        double avg_simd1 = (double)s1.simd_iters / (double)num_queries;
        double avg_scalar1 = (double)s1.scalar_iters / (double)num_queries;
        printf("  Phase1 cmpgt(vec,key):   %8.1f cy/q  simd_iter=%.1f scalar_iter=%.1f hits=%d\n",
               cy1, avg_simd1, avg_scalar1, s1.hits);

        query_stats_t s2 = bench_pocv3(data, n, queries, num_queries);
        double cy2 = (double)s2.cycles / (double)num_queries;
        double avg_simd2 = (double)s2.simd_iters / (double)num_queries;
        double avg_scalar2 = (double)s2.scalar_iters / (double)num_queries;
        printf("  POCv3  cmpgt(key,vec):   %8.1f cy/q  simd_iter=%.1f scalar_iter=%.1f hits=%d\n",
               cy2, avg_simd2, avg_scalar2, s2.hits);

        printf("  Phase1/POCv3 ratio:      %.2fx\n", cy2 / cy1);
        printf("  Phase1 vs Scalar:        %.2fx\n", scalar_cy / cy1);
        printf("  POCv3 vs Scalar:         %.2fx\n", scalar_cy / cy2);
        printf("\n");

        _mm_free(data);
        free(queries);
    }

    printf("DEEP-05 Analysis Complete.\n");
    return 0;
}
