#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>

#define ANCHOR_INTERVAL 256
#define LO16_THRESHOLD  256

static void *aligned_malloc(size_t size, size_t alignment) {
    return _mm_malloc(size, alignment);
}

static void aligned_free(void *ptr) {
    _mm_free(ptr);
}

static uint64_t rdtsc(void) {
    return __rdtsc();
}

static int compare_int32(const void *a, const void *b) {
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static int32_t *generate_sorted_data(size_t n) {
    int32_t *data = (int32_t *)aligned_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    for (size_t i = 0; i < n; i++) data[i] = (int32_t)((uint32_t)rand() ^ ((uint32_t)rand() << 15));
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

int32_t search_scalar_binary(const int32_t *arr, size_t n, int32_t target) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < n && arr[lo] == target) return (int32_t)lo;
    return -1;
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

        int le_count = __builtin_popcount(mask ^ 0xFF);

        if (le_count == 8) {
            lo = block + 8;
        } else if (le_count == 0) {
            hi = block;
        } else {
            size_t idx = block + (size_t)le_count;
            if (arr[idx] == target) return (int32_t)idx;
            lo = block + (size_t)le_count;
            hi = block + (size_t)le_count;
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

typedef struct {
    int32_t *anchors;
    size_t   anchor_count;
    size_t   interval;
} anchor_index_t;

static anchor_index_t *anchor_build(const int32_t *arr, size_t n, size_t interval) {
    anchor_index_t *idx = (anchor_index_t *)malloc(sizeof(anchor_index_t));
    idx->interval = interval;
    idx->anchor_count = (n + interval - 1) / interval;
    idx->anchors = (int32_t *)aligned_malloc(idx->anchor_count * sizeof(int32_t), 32);
    for (size_t i = 0; i < idx->anchor_count; i++) {
        size_t pos = i * interval;
        if (pos >= n) pos = n - 1;
        idx->anchors[i] = arr[pos];
    }
    return idx;
}

static void anchor_free(anchor_index_t *idx) {
    if (idx) {
        aligned_free(idx->anchors);
        free(idx);
    }
}

int32_t search_anchor_avx2(const anchor_index_t *anchor, const int32_t *arr, size_t n,
                           int32_t target) {
    if (n == 0) return -1;

    size_t a_lo = 0, a_hi = anchor->anchor_count;
    while (a_lo < a_hi) {
        size_t a_mid = a_lo + (a_hi - a_lo) / 2;
        if (anchor->anchors[a_mid] <= target)
            a_lo = a_mid + 1;
        else
            a_hi = a_mid;
    }

    size_t block_start = (a_lo > 0) ? (a_lo - 1) * anchor->interval : 0;
    size_t block_end = (a_lo + 1) * anchor->interval;
    if (block_end > n) block_end = n;

    if (block_end <= block_start) return -1;

    size_t lo = block_start, hi = block_end;
    while (hi - lo >= 8) {
        size_t mid = lo + (hi - lo) / 2;
        size_t block = mid & ~(size_t)7;
        if (block < lo) block = lo;
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
            if (arr[idx] == target) return (int32_t)idx;
            return -1;
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

typedef struct {
    int32_t  *vals;
    uint16_t *lo16;
    size_t    count;
    anchor_index_t *anchor;
} lo16_index_t;

static lo16_index_t *lo16_index_build(const int32_t *arr, size_t n, size_t interval) {
    lo16_index_t *idx = (lo16_index_t *)malloc(sizeof(lo16_index_t));
    idx->count = n;
    idx->vals  = (int32_t *)aligned_malloc(n * sizeof(int32_t), 32);
    idx->lo16  = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
    memcpy(idx->vals, arr, n * sizeof(int32_t));
    for (size_t i = 0; i < n; i++) {
        idx->lo16[i] = (uint16_t)(arr[i] & 0xFFFF);
    }
    idx->anchor = anchor_build(arr, n, interval);
    return idx;
}

static void lo16_index_free(lo16_index_t *idx) {
    if (idx) {
        aligned_free(idx->vals);
        aligned_free(idx->lo16);
        anchor_free(idx->anchor);
        free(idx);
    }
}

int32_t search_lo16_scan(const lo16_index_t *idx, int32_t target) {
    size_t n = idx->count;
    if (n == 0) return -1;

    anchor_index_t *anchor = idx->anchor;

    size_t a_lo = 0, a_hi = anchor->anchor_count;
    while (a_lo < a_hi) {
        size_t a_mid = a_lo + (a_hi - a_lo) / 2;
        if (anchor->anchors[a_mid] <= target)
            a_lo = a_mid + 1;
        else
            a_hi = a_mid;
    }

    size_t win_start = (a_lo > 0) ? (a_lo - 1) * anchor->interval : 0;
    size_t win_end   = (a_lo + 1) * anchor->interval;
    if (win_end > n) win_end = n;
    if (win_end <= win_start) return -1;

    uint16_t target_lo16 = (uint16_t)(target & 0xFFFF);
    __m256i key = _mm256_set1_epi16((int16_t)target_lo16);

    size_t i = win_start;
    size_t tail_start = win_end & ~(size_t)15;

    for (; i < tail_start; i += 16) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)(idx->lo16 + i));
        __m256i eq = _mm256_cmpeq_epi16(key, chunk);
        int mask = _mm256_movemask_epi8(eq);

        if (mask != 0) {
            int m = mask;
            while (m != 0) {
                int bit2 = __builtin_ctz((unsigned int)m);
                size_t pos = i + (size_t)(bit2 >> 1);
                if (pos < n && idx->vals[pos] == target)
                    return (int32_t)pos;
                m &= m - 1;
            }
        }
    }

    for (; i < win_end; i++) {
        if (idx->lo16[i] == target_lo16 && idx->vals[i] == target)
            return (int32_t)i;
    }

    return -1;
}

typedef struct {
    const char *name;
    int32_t (*search)(void *ctx, int32_t target);
    void *ctx;
    void (*cleanup)(void *ctx);
} bench_case_t;

typedef struct {
    const int32_t *arr;
    size_t n;
} scalar_ctx_t;

static int32_t bench_scalar(void *ctx, int32_t target) {
    scalar_ctx_t *c = (scalar_ctx_t *)ctx;
    return search_scalar_binary(c->arr, c->n, target);
}

typedef struct {
    const int32_t *arr;
    size_t n;
} avx2_ctx_t;

static int32_t bench_avx2(void *ctx, int32_t target) {
    avx2_ctx_t *c = (avx2_ctx_t *)ctx;
    return search_avx2_binary(c->arr, c->n, target);
}

typedef struct {
    const anchor_index_t *anchor;
    const int32_t *arr;
    size_t n;
} anchor_ctx_t;

static int32_t bench_anchor(void *ctx, int32_t target) {
    anchor_ctx_t *c = (anchor_ctx_t *)ctx;
    return search_anchor_avx2(c->anchor, c->arr, c->n, target);
}

static int32_t bench_lo16(void *ctx, int32_t target) {
    lo16_index_t *c = (lo16_index_t *)ctx;
    return search_lo16_scan(c, target);
}

static void cleanup_nop(void *ctx) { (void)ctx; }
static void cleanup_anchor(void *ctx) { anchor_free((anchor_index_t *)ctx); }
static void cleanup_lo16(void *ctx) { lo16_index_free((lo16_index_t *)ctx); }

static void run_benchmark(const char *label, const int32_t *data, size_t n,
                          const int32_t *queries, size_t num_queries) {
    printf("\n========================================\n");
    printf("  %s  (N=%zu)\n", label, n);
    printf("========================================\n");

    lo16_index_t  *lo16_idx = lo16_index_build(data, n, ANCHOR_INTERVAL);
    anchor_index_t *anchor  = anchor_build(data, n, ANCHOR_INTERVAL);

    scalar_ctx_t scalar_ctx = { data, n };
    avx2_ctx_t   avx2_ctx   = { data, n };
    anchor_ctx_t anchor_ctx = { anchor, data, n };

    bench_case_t cases[] = {
        { "标量二分",         bench_scalar, &scalar_ctx, cleanup_nop },
        { "AVX2 SIMD二分",    bench_avx2,   &avx2_ctx,   cleanup_nop },
        { "锚点+AVX2二分(方案A')", bench_anchor, &anchor_ctx, cleanup_nop },
        { "high16锚点+lo16扫描(方案B)", bench_lo16, lo16_idx, cleanup_nop },
    };
    int num_cases = sizeof(cases) / sizeof(cases[0]);

    int num_warmup = num_queries > 100 ? 100 : (int)num_queries;

    for (int c = 0; c < num_cases; c++) {
        volatile int32_t discard = 0;

        for (int w = 0; w < num_warmup; w++)
            discard |= cases[c].search(cases[c].ctx, queries[w]);
        (void)discard;

        uint64_t t0 = rdtsc();
        for (size_t q = 0; q < num_queries; q++)
            discard |= cases[c].search(cases[c].ctx, queries[q]);
        uint64_t t1 = rdtsc();
        (void)discard;

        double cycles_per_query = (double)(t1 - t0) / (double)num_queries;
        double ns_per_query = cycles_per_query / 3.5;

        printf("  %-30s  %8.1f cycles/q  (%7.1f ns/q)\n",
               cases[c].name, cycles_per_query, ns_per_query);
        fflush(stdout);
    }

    cleanup_anchor(anchor);
    cleanup_lo16(lo16_idx);
}

static void run_data_size(const char *label, size_t n, size_t num_queries) {
    printf("\n>>> Building %s sorted data (%zu elements)...\n", label, n);
    fflush(stdout);
    int32_t *data = generate_sorted_data(n);
    int32_t *queries = generate_queries(data, n, num_queries, 50);

    run_benchmark(label, data, n, queries, num_queries);

    aligned_free(data);
    free(queries);
}

int main(void) {
    srand((unsigned int)time(NULL));

    printf("Int32 Search Algorithm POC Benchmark\n");
    printf("Anchor interval: %d\n", ANCHOR_INTERVAL);
    printf("CPU freq assumed: 3.5 GHz for ns conversion (approximate)\n");
    printf("Note: Use actual CPU freq for precise ns values.\n");

    size_t num_queries = 1000000;

    run_data_size("1M  data", 1000000, num_queries);
    run_data_size("5M  data", 5000000, num_queries);
    run_data_size("10M data", 10000000, num_queries);

    printf("\nDone.\n");
    return 0;
}
