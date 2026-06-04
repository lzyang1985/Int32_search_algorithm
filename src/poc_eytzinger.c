#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

static int compare_int32(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static int32_t *generate_sorted_data(size_t n)
{
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
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

static size_t next_pow2(size_t n)
{
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

static void eytzinger_build_rec(const int32_t *sorted, int32_t *tree,
                                 size_t sorted_start, size_t sorted_end,
                                 size_t tree_idx)
{
    if (sorted_start >= sorted_end) return;
    size_t mid = sorted_start + (sorted_end - sorted_start) / 2;
    tree[tree_idx] = sorted[mid];
    eytzinger_build_rec(sorted, tree, sorted_start, mid, 2 * tree_idx + 1);
    eytzinger_build_rec(sorted, tree, mid + 1, sorted_end, 2 * tree_idx + 2);
}

static int32_t *build_eytzinger(const int32_t *sorted, size_t n)
{
    if (n == 0) return NULL;
    size_t cap = next_pow2(n + 1);
    int32_t *tree = (int32_t *)malloc(cap * sizeof(int32_t));
    if (!tree) return NULL;
    memset(tree, 0x80, cap * sizeof(int32_t));
    eytzinger_build_rec(sorted, tree, 0, n, 0);
    return tree;
}

static int32_t search_eytzinger(const int32_t *tree, size_t n, int32_t target, size_t *result_idx)
{
    if (n == 0 || tree == NULL) return -1;
    size_t cap = next_pow2(n + 1);
    size_t idx = 0;
    size_t found_idx = 0;
    int found = 0;

    while (idx < cap) {
        int32_t v = tree[idx];
        if (v == (int32_t)0x80808080) break;
        if (v == target) {
            found = 1;
            found_idx = idx;
        }
        idx = 2 * idx + 1 + (target > v ? 1 : 0);
    }

    if (found) {
        if (result_idx) {
            size_t lo = 0, hi = n;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (tree[found_idx] < target) lo = mid + 1;
                else hi = mid;
            }
            *result_idx = lo;
        }
        return 0;
    }
    return -1;
}

static int32_t search_scalar_bs(const int32_t *vals, size_t n, int32_t target)
{
    if (n == 0) return -1;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo < n && vals[lo] == target) return 0;
    return -1;
}

static double bench_eytzinger(const int32_t *tree, size_t n,
                               const int32_t *queries, size_t num_queries,
                               int *out_hits)
{
    volatile int discard = 0;

    for (size_t i = 0; i < 100; i++) {
        size_t idx = 0;
        discard |= search_eytzinger(tree, n, queries[i], &idx);
    }

    int hits = 0;
    unsigned long long t0 = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        size_t idx = 0;
        int r = search_eytzinger(tree, n, queries[q], &idx);
        if (r == 0) hits++;
        discard |= (int)idx;
    }
    unsigned long long t1 = rdtscp_bench();
    (void)discard;

    *out_hits = hits;
    return (double)(t1 - t0) / (double)num_queries;
}

static double bench_scalar(const int32_t *vals, size_t n,
                            const int32_t *queries, size_t num_queries,
                            int *out_hits)
{
    volatile int discard = 0;

    for (size_t i = 0; i < 100; i++)
        discard |= search_scalar_bs(vals, n, queries[i]);

    int hits = 0;
    unsigned long long t0 = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        int r = search_scalar_bs(vals, n, queries[q]);
        if (r == 0) hits++;
        discard += r;
    }
    unsigned long long t1 = rdtscp_bench();
    (void)discard;

    *out_hits = hits;
    return (double)(t1 - t0) / (double)num_queries;
}

static void run_boundary_test(void)
{
    printf("Eytzinger Boundary Test\n");
    size_t sizes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                      63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513,
                      1023, 1024, 1025, 2047, 2048, 2049, 4095, 4096, 4097,
                      8191, 8192, 8193, 16383, 16384, 16385, 32767, 32768, 32769,
                      65535, 65536, 65537};
    int n_tests = sizeof(sizes) / sizeof(sizes[0]);
    int failures = 0;

    for (int t = 0; t < n_tests; t++) {
        size_t n = sizes[t];
        int32_t *data = NULL;
        if (n > 0) {
            data = (int32_t *)malloc(n * sizeof(int32_t));
            if (!data) { failures++; continue; }
            for (size_t i = 0; i < n; i++)
                data[i] = (int32_t)(i * 2 + 10);
        }

        int32_t *tree = NULL;
        if (n > 0) {
            tree = build_eytzinger(data, n);
            if (!tree) { failures++; free(data); continue; }
        }

        int32_t test_keys[] = {
            9, 10, 11, 100, 200,
            (int32_t)(2 * (n > 0 ? n - 1 : 0) + 10),
            (int32_t)(2 * n + 10),
            (int32_t)(2 * n + 11)
        };
        int n_keys = sizeof(test_keys) / sizeof(test_keys[0]);

        for (int k = 0; k < n_keys; k++) {
            size_t eytz_idx = 0;
            int ey_rc = search_eytzinger(tree, n, test_keys[k], &eytz_idx);
            int sc_rc = search_scalar_bs(data, n, test_keys[k]);
            if ((ey_rc == 0) != (sc_rc == 0)) {
                failures++;
                fprintf(stderr, "BOUNDARY MISMATCH: n=%zu key=%d ey=%d sc=%d\n",
                        n, test_keys[k], ey_rc, sc_rc);
            }
        }

        free(tree);
        free(data);
    }

    printf("  boundary: %d sizes, %d failures\n", n_tests, failures);
}

int main(void)
{
    srand((unsigned int)time(NULL));
    printf("Eytzinger Layout POC\n");
    printf("====================\n");
    printf("Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    printf("\n");

    run_boundary_test();

    size_t sizes[] = {1000000, 10000000};
    int n_sizes = 2;

    for (int si = 0; si < n_sizes; si++) {
        size_t n = sizes[si];
        size_t num_queries = 1000000;
        double ram_mb = (double)(next_pow2(n + 1) * sizeof(int32_t)) / (1024.0 * 1024.0);

        printf("\n--- N = %zu (%.1fM), RAM = %.0f MB ---\n", n, n / 1000000.0, ram_mb);

        srand(12345 + si);
        int32_t *data = generate_sorted_data(n);
        int32_t *queries = generate_queries(data, n, num_queries);

        int32_t *tree = build_eytzinger(data, n);
        if (!tree) {
            printf("  ERROR: build_eytzinger failed (OOM)\n");
            free(data);
            free(queries);
            continue;
        }

        int e_hits, s_hits;
        double e_cy = bench_eytzinger(tree, n, queries, num_queries, &e_hits);
        double s_cy = bench_scalar(data, n, queries, num_queries, &s_hits);

        printf("  Eytzinger: %8.1f cy/q  (hits=%d)\n", e_cy, e_hits);
        printf("  Scalar:    %8.1f cy/q  (hits=%d)\n", s_cy, s_hits);
        printf("  Speedup:   %.2fx vs scalar\n", s_cy / e_cy);
        printf("  RAM:       %.0f MB (%.1fx sorted array)\n",
               ram_mb, ram_mb / ((double)(n * sizeof(int32_t)) / (1024.0 * 1024.0)));

        free(tree);
        free(data);
        free(queries);
    }

    printf("\nDone.\n");
    return 0;
}
