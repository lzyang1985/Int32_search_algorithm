#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "int32_search.h"
#include "bench_data_gen.h"
#include "search_avx2.h"
#include "search_scalar.h"
#include "platform_cpu.h"

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <immintrin.h>

static void get_cpu_name(char *buf, size_t bufsz)
{
    int regs[12];
    __asm__ __volatile__(
        "cpuid"
        : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
        : "a"(0x80000000)
    );
    if ((unsigned int)regs[0] < 0x80000004) {
        snprintf(buf, bufsz, "(unknown)");
        return;
    }
    __asm__ __volatile__(
        "cpuid"
        : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
        : "a"(0x80000002)
    );
    memcpy(buf, regs, 16);
    __asm__ __volatile__(
        "cpuid"
        : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
        : "a"(0x80000003)
    );
    memcpy(buf + 16, regs, 16);
    __asm__ __volatile__(
        "cpuid"
        : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
        : "a"(0x80000004)
    );
    memcpy(buf + 32, regs, 16);
    buf[48] = '\0';
    for (int i = 47; i >= 0 && (buf[i] == ' ' || buf[i] == '\0'); i--)
        buf[i] = '\0';
}

static void print_cpu_info(void)
{
    char name[64];
    get_cpu_name(name, sizeof(name));
    printf("CPU: %s\n", name);
    printf("AVX2 detected (platform_cpu_has_avx2): %s\n",
           platform_cpu_has_avx2() ? "yes" : "no");
    printf("AVX2 detected (__builtin_cpu_supports): %s\n",
           __builtin_cpu_supports("avx2") ? "yes" : "no");
    printf("Compiler: GCC %d.%d.%d\n",
           __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
}

static unsigned long long rdtscp_bench(void)
{
    unsigned int aux;
    unsigned long long t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

static void warmup_long(int32_search_t h, const int32_t *queries,
                         size_t num_queries, unsigned long long target_cycles)
{
    volatile int discard = 0;
    unsigned long long t0 = rdtscp_bench();
    size_t i = 0;
    while (1) {
        size_t idx;
        discard |= int32_search_find(h, queries[i % num_queries], &idx);
        i++;
        if (i % 1024 == 0) {
            unsigned long long t1 = rdtscp_bench();
            if (t1 - t0 >= target_cycles) break;
        }
    }
    (void)discard;
}

static double run_api_benchmark(int32_search_t h, const int32_t *queries,
                                 size_t num_queries, const char *label)
{
    volatile int discard = 0;

    warmup_long(h, queries, num_queries,
                (unsigned long long)500 * 3000 * 1000);

    unsigned long long t_start = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        size_t idx;
        discard |= int32_search_find(h, queries[q], &idx);
    }
    unsigned long long t_end = rdtscp_bench();
    (void)discard;

    double cy = (double)(t_end - t_start) / (double)num_queries;
    printf("  %-32s %8.1f cy/q\n", label, cy);
    return cy;
}

static double run_raw_avx2_bench(const int32_t *vals, size_t n,
                                  const int32_t *queries, size_t num_queries,
                                  const char *label)
{
    volatile int discard = 0;

    {
        unsigned long long t0 = rdtscp_bench();
        unsigned long long target = t0 + (unsigned long long)500 * 3000 * 1000;
        size_t i = 0;
        while (1) {
            size_t idx;
            discard |= search_avx2_find(vals, n, queries[i % num_queries], &idx);
            i++;
            if (i % 1024 == 0) {
                if (rdtscp_bench() >= target) break;
            }
        }
    }

    unsigned long long t_start = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        size_t idx;
        discard |= search_avx2_find(vals, n, queries[q], &idx);
    }
    unsigned long long t_end = rdtscp_bench();
    (void)discard;

    double cy = (double)(t_end - t_start) / (double)num_queries;
    printf("  %-32s %8.1f cy/q\n", label, cy);
    return cy;
}

static double run_raw_scalar_bench(const int32_t *vals, size_t n,
                                    const int32_t *queries, size_t num_queries,
                                    const char *label)
{
    volatile int discard = 0;

    {
        unsigned long long t0 = rdtscp_bench();
        unsigned long long target = t0 + (unsigned long long)500 * 3000 * 1000;
        size_t i = 0;
        while (1) {
            size_t idx;
            discard |= search_scalar_find(vals, n, queries[i % num_queries], &idx);
            i++;
            if (i % 1024 == 0) {
                if (rdtscp_bench() >= target) break;
            }
        }
    }

    unsigned long long t_start = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        size_t idx;
        discard |= search_scalar_find(vals, n, queries[q], &idx);
    }
    unsigned long long t_end = rdtscp_bench();
    (void)discard;

    double cy = (double)(t_end - t_start) / (double)num_queries;
    printf("  %-32s %8.1f cy/q\n", label, cy);
    return cy;
}

static double run_inline_scalar_bench(const int32_t *vals, size_t n,
                                       const int32_t *queries, size_t num_queries,
                                       const char *label)
{
    volatile int discard = 0;

    {
        unsigned long long t0 = rdtscp_bench();
        unsigned long long target = t0 + (unsigned long long)500 * 3000 * 1000;
        size_t i = 0;
        while (1) {
            int32_t key = queries[i % num_queries];
            size_t lo = 0, hi = n;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (vals[mid] < key) lo = mid + 1;
                else hi = mid;
            }
            if (lo < n && vals[lo] == key) discard |= (int)lo;
            i++;
            if (i % 1024 == 0) {
                if (rdtscp_bench() >= target) break;
            }
        }
    }

    unsigned long long t_start = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        int32_t key = queries[q];
        size_t lo = 0, hi = n;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (vals[mid] < key) lo = mid + 1;
            else hi = mid;
        }
        if (lo < n && vals[lo] == key) discard |= (int)lo;
    }
    unsigned long long t_end = rdtscp_bench();
    (void)discard;

    double cy = (double)(t_end - t_start) / (double)num_queries;
    printf("  %-32s %8.1f cy/q\n", label, cy);
    return cy;
}

static void run_fair_benchmark(const int32_t *data, size_t n,
                                const int32_t *queries, size_t num_queries,
                                int32_search_t h)
{
    printf("\n=== Fair Benchmark (5-group) ===\n");
    printf("  %-32s %s\n", "Group", "cy/query");
    printf("  --------------------------------------------------\n");

    double api_avx2_cy  = run_api_benchmark(h, queries, num_queries,
                                             "01 API (AVX2 via CPU detect)");
    double raw_avx2_cy  = run_raw_avx2_bench(data, n, queries, num_queries,
                                              "02 Raw AVX2 (search_avx2_find)");
    double raw_scalar_cy = run_raw_scalar_bench(data, n, queries, num_queries,
                                                 "03 Raw Scalar (search_scalar_find)");
    double inline_cy    = run_inline_scalar_bench(data, n, queries, num_queries,
                                                   "04 Inline Scalar (no func call)");

    printf("  --------------------------------------------------\n");
    printf("  %-32s %8.2fx\n", "AVX2 vs Raw Scalar speedup",
           raw_scalar_cy / raw_avx2_cy);
    printf("  %-32s %8.2fx\n", "API AVX2 vs Inline Scalar speedup",
           inline_cy / api_avx2_cy);
}

static void run_data_size_test(size_t n)
{
    printf("\n----------------------------------------\n");
    printf("  N = %zu (%.2fM)\n", n, n / 1000000.0);
    printf("----------------------------------------\n");

    int32_t *data = bench_generate_sorted_data(n);
    if (data == NULL) { printf("  data gen failed\n"); return; }

    size_t num_queries = 1000000;
    int32_t *queries = bench_generate_queries(data, n, num_queries, 50);
    if (queries == NULL) { free(data); return; }

    int32_search_t h = int32_search_create(data, n, NULL);
    if (h == NULL) { printf("  create failed\n"); free(queries); free(data); return; }

    run_fair_benchmark(data, n, queries, num_queries, h);

    int32_search_destroy(h);
    free(queries);
    free(data);
}

int main(void)
{
    {
        const char *seed_env = getenv("INT32SEARCH_BENCH_SEED");
        srand(seed_env ? (unsigned int)atoi(seed_env)
                       : (unsigned int)time(NULL));
    }

    printf("=== Int32 Search Algorithm — Windows AVX2 Investigation ===\n\n");
    printf("--- CPU & Environment Info ---\n");
    print_cpu_info();
    printf("\nTiming: __rdtscp() + _mm_lfence()\n");
    printf("Warmup: ~500ms per group (Turbo ramp-up eliminated)\n");
    printf("Queries: 1M per test, 50%% hit rate\n");
    printf("Distribution: uniform random\n");

    run_data_size_test(1000000);
    run_data_size_test(5000000);
    run_data_size_test(10000000);

    printf("\nDone.\n");
    return 0;
}
