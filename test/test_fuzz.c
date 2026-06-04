#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "int32_search.h"
#include "search_avx2.h"
#include "search_scalar.h"
#include "platform_cpu.h"

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#define FUZZ_SECONDS      60
#define MAX_N             65536
#define FUZZ_PRINT_EVERY  1000000

static int compare_int32(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static int32_t rand_int32(void)
{
    return (int32_t)((uint32_t)rand() ^ ((uint32_t)rand() << 15));
}

static void shuffle(int32_t *arr, size_t n)
{
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)rand() % (i + 1);
        int32_t tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

static int test_fuzz_round(void)
{
    size_t n = (size_t)(rand() % MAX_N) + 1;
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    if (!data) return 0;

    for (size_t i = 0; i < n; i++)
        data[i] = rand_int32();

    qsort(data, n, sizeof(int32_t), compare_int32);

    int32_search_t handle = int32_search_create(data, n, NULL);
    if (!handle) {
        free(data);
        return 0;
    }

    size_t num_queries = (size_t)(rand() % 1000) + 1;
    int32_t *queries = (int32_t *)malloc(num_queries * sizeof(int32_t));
    if (!queries) {
        int32_search_destroy(handle);
        free(data);
        return 0;
    }

    size_t hit_count = (size_t)((uint64_t)num_queries * (rand() % 100) / 100);
    for (size_t i = 0; i < hit_count; i++)
        queries[i] = data[(size_t)rand() % n];

    for (size_t i = hit_count; i < num_queries; i++)
        queries[i] = rand_int32();

    shuffle(queries, num_queries);

    int mismatches = 0;
    int have_avx2 = platform_cpu_has_avx2();

    for (size_t q = 0; q < num_queries; q++) {
        int32_t key = queries[q];

        size_t api_idx;
        int api_rc = int32_search_find(handle, key, &api_idx);

        size_t scalar_idx;
        int scalar_rc = search_scalar_find(data, n, key, &scalar_idx);

        if (api_rc != scalar_rc || (api_rc == INT32_SEARCH_OK && api_idx != scalar_idx)) {
            mismatches++;
            fprintf(stderr, "\nFUZZ MISMATCH: n=%zu key=%d api_rc=%d api_idx=%zu scalar_rc=%d scalar_idx=%zu\n",
                    n, key, api_rc, api_idx, scalar_rc, scalar_idx);
            break;
        }

        if (have_avx2) {
            size_t avx2_idx;
            int avx2_rc = search_avx2_find(data, n, key, &avx2_idx);
            if (avx2_rc != scalar_rc || (avx2_rc == INT32_SEARCH_OK && avx2_idx != scalar_idx)) {
                mismatches++;
                fprintf(stderr, "\nFUZZ MISMATCH (AVX2): n=%zu key=%d avx2_rc=%d avx2_idx=%zu scalar_rc=%d scalar_idx=%zu\n",
                        n, key, avx2_rc, avx2_idx, scalar_rc, scalar_idx);
                break;
            }
        }
    }

    free(queries);
    int32_search_destroy(handle);
    free(data);
    return mismatches == 0 ? 0 : 1;
}

static void test_boundary_sweep(void)
{
    printf("fuzz: boundary sweep n=0..65537\n");

    size_t sizes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33,
                      63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513,
                      1023, 1024, 1025, 2047, 2048, 2049, 4095, 4096, 4097,
                      8191, 8192, 8193, 16383, 16384, 16385, 32767, 32768, 32769,
                      65535, 65536, 65537};
    int n_tests = sizeof(sizes) / sizeof(sizes[0]);

    int failures = 0;
    for (int t = 0; t < n_tests; t++) {
        size_t n = sizes[t];
        if (t > 0 && n <= sizes[t-1]) continue;

        int32_t *data = NULL;
        if (n > 0) {
            data = (int32_t *)malloc(n * sizeof(int32_t));
            if (!data) { failures++; continue; }
            for (size_t i = 0; i < n; i++)
                data[i] = (int32_t)(i * 2 + 10);
        }

        int32_t test_keys[] = {
            9, 10, 11, 100, 200, 300,
            (int32_t)(2 * (n - 1) + 10),
            (int32_t)(2 * n + 10),
            (int32_t)(2 * n + 11)
        };
        int n_keys = sizeof(test_keys) / sizeof(test_keys[0]);

        for (int k = 0; k < n_keys; k++) {
            size_t avx2_idx;
            int avx2_rc = search_avx2_find(data, n, test_keys[k], &avx2_idx);

            size_t scalar_idx;
            int scalar_rc = search_scalar_find(data, n, test_keys[k], &scalar_idx);

            if (avx2_rc != scalar_rc || (avx2_rc == INT32_SEARCH_OK && avx2_idx != scalar_idx)) {
                failures++;
                fprintf(stderr, "BOUNDARY FAIL: n=%zu key=%d\n", n, test_keys[k]);
            }
        }

        if (data) free(data);
    }

    printf("  boundary sweep: %d sizes, %d failures\n", n_tests, failures);
}

int main(void)
{
    srand((unsigned int)time(NULL));
    printf("fuzz: Int32 Search Fuzz Test\n");
    printf("fuzz: duration=%ds  max_n=%d\n", FUZZ_SECONDS, MAX_N);
    printf("fuzz: AVX2 detected: %s\n", platform_cpu_has_avx2() ? "yes" : "no");
    fflush(stdout);

    test_boundary_sweep();

    time_t start = time(NULL);
    size_t round = 0;
    size_t mismatches = 0;

    while (1) {
        if (test_fuzz_round() != 0)
            mismatches++;

        round++;
        if (round % FUZZ_PRINT_EVERY == 0) {
            time_t elapsed = time(NULL) - start;
            printf("fuzz: round=%zu mismatches=%zu elapsed=%lds\n",
                   round, mismatches, (long)elapsed);
            fflush(stdout);
        }

        if (time(NULL) - start >= FUZZ_SECONDS)
            break;
    }

    time_t elapsed = time(NULL) - start;
    printf("\nfuzz: DONE. rounds=%zu mismatches=%zu time=%lds\n",
           round, mismatches, (long)elapsed);

    if (mismatches == 0)
        printf("fuzz: ALL PASS — no correctness discrepancies\n");
    else
        printf("fuzz: FAIL — %zu discrepancy rounds\n", mismatches);

    return mismatches > 0 ? 1 : 0;
}
