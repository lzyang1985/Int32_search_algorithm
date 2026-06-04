#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>

#define DIR_BUCKETS 65537

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

static int32_t *generate_skewed_data(size_t n) {
    int32_t *data = (int32_t *)aligned_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "malloc failed\n"); exit(1); }
    size_t hot = n * 80 / 100;
    for (size_t i = 0; i < hot; i++)
        data[i] = (int32_t)((uint32_t)rand() & 0x0000FFFF);
    for (size_t i = hot; i < n; i++)
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

    uint16_t prev_h = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        uint16_t h = (uint16_t)((uint32_t)vals[i] >> 16);
        if (h != prev_h) {
            dir[h] = (int32_t)i;
            prev_h = h;
        }
    }

    for (int32_t i = DIR_BUCKETS - 2; i >= 0; i--) {
        if (dir[i] == -1) dir[i] = dir[i + 1];
    }
    dir[DIR_BUCKETS - 1] = (int32_t)n;
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

int32_t search_b2_high16_avx2(const int32_t *vals, const int32_t *dir,
                               size_t n, int32_t target) {
    if (n == 0) return -1;
    uint16_t h = (uint16_t)((uint32_t)target >> 16);
    int32_t start = dir[h];
    int32_t end   = (h < 65535) ? dir[h + 1] : (int32_t)n;
    if (start >= end) return -1;

    size_t lo = (size_t)start, hi = (size_t)end;
    while (hi - lo >= 8) {
        size_t mid = lo + (hi - lo) / 2;
        size_t block = mid & ~(size_t)7;
        if (block < lo) block = lo;
        if (block + 8 > hi) block = hi - 8;

        __m256i k = _mm256_set1_epi32(target);
        __m256i v = _mm256_loadu_si256((const __m256i *)(vals + block));
        __m256i cmp = _mm256_cmpgt_epi32(k, v);
        int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        int gt_count = __builtin_popcount(mask);

        if (gt_count == 0) {
            hi = block;
        } else if (gt_count == 8) {
            lo = block + 8;
        } else {
            size_t idx = block + (size_t)gt_count;
            if (vals[idx] == target) return (int32_t)idx;
            return -1;
        }
    }

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    if (lo < (size_t)end && vals[lo] == target) return (int32_t)lo;
    return -1;
}

typedef struct {
    const char *name;
    int32_t (*search)(void *ctx, int32_t target);
    void *ctx;
} bench_case_t;

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

typedef struct {
    const int32_t *vals;
    const int32_t *dir;
    size_t n;
} b2_ctx_t;

static int32_t bench_b2(void *ctx, int32_t target) {
    b2_ctx_t *c = (b2_ctx_t *)ctx;
    return search_b2_high16_avx2(c->vals, c->dir, c->n, target);
}

static void run_benchmark(const char *label, const int32_t *data, size_t n,
                          const int32_t *queries, size_t num_queries) {
    printf("\n========================================\n");
    printf("  %s  (N=%zu)\n", label, n);
    printf("========================================\n");

    uint16_t *lo16 = (uint16_t *)aligned_malloc(n * sizeof(uint16_t), 32);
    for (size_t i = 0; i < n; i++) lo16[i] = (uint16_t)(data[i] & 0xFFFF);

    int32_t *dir = (int32_t *)aligned_malloc(DIR_BUCKETS * sizeof(int32_t), 32);
    high16_dir_build(data, n, dir);

    avx2_ctx_t avx2_ctx = { data, n };
    b1_ctx_t   b1_ctx   = { data, lo16, dir, n };
    b2_ctx_t   b2_ctx   = { data, dir, n };

    bench_case_t cases[] = {
        { "A: AVX2 SIMD二分",      bench_avx2, &avx2_ctx },
        { "B1: high16dir+lo16扫描", bench_b1,   &b1_ctx },
        { "B2: high16dir+AVX2二分", bench_b2,   &b2_ctx },
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

        double cy = (double)(t1 - t0) / (double)num_queries;
        printf("  %-28s  %8.1f cycles/q\n", cases[c].name, cy);
        fflush(stdout);
    }

    aligned_free(lo16);
    aligned_free(dir);
}

static void run_data_size(const char *label, size_t n, size_t num_queries, int skewed) {
    printf("\n>>> Building %s (%zu elements, %s)...\n", label, n,
           skewed ? "skewed" : "uniform");
    fflush(stdout);
    int32_t *data = skewed ? generate_skewed_data(n) : generate_sorted_data(n);
    int32_t *queries = generate_queries(data, n, num_queries, 50);
    run_benchmark(label, data, n, queries, num_queries);
    aligned_free(data);
    free(queries);
}

static void print_dir_stats(const int32_t *dir) {
    size_t non_empty = 0, max_size = 0, total = 0;
    for (int i = 0; i < 65536; i++) {
        int32_t sz = dir[i + 1] - dir[i];
        if (sz > 0) non_empty++;
        if ((size_t)sz > max_size) max_size = (size_t)sz;
        total += (size_t)sz;
    }
    printf("  Dir stats: %zu non-empty buckets, max=%zu, avg=%.1f\n",
           non_empty, max_size, (double)total / (double)non_empty);
    fflush(stdout);
}

int main(void) {
    srand((unsigned int)time(NULL));

    printf("Int32 Search Algorithm POC Benchmark v2\n");
    printf("high16 directory: %d buckets (262KB)\n", DIR_BUCKETS - 1);
    printf("Compiled: gcc -O3 -mavx2 -march=native\n\n");

    size_t num_queries = 1000000;

    run_data_size("1M uniform", 1000000, num_queries, 0);
    run_data_size("5M uniform", 5000000, num_queries, 0);
    run_data_size("10M uniform", 10000000, num_queries, 0);
    run_data_size("10M skewed ", 10000000, num_queries, 1);

    printf("\nDone.\n");
    return 0;
}
