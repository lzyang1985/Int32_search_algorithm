#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "int32_search.h"

static int compare_int32(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static double get_time_sec(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

int main(void)
{
    size_t n = 5000000;
    size_t num_queries = 10000;
    int hit_ratio = 50;

    srand((unsigned int)time(NULL));

    printf("=== Int32 Search Demo (Bloom OFF) ===\n");
    printf("Data size:      5,000,000\n");
    printf("Queries:        10,000 (hit rate ~%d%%)\n", hit_ratio);
    printf("Bloom filter:   disabled (use_bloom=0)\n");
    printf("\n");

    printf("Generating %zu random int32 numbers...\n", n);
    fflush(stdout);
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    if (!data) {
        printf("ERROR: malloc failed for data\n");
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        data[i] = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
    }
    qsort(data, n, sizeof(int32_t), compare_int32);

    printf("Building search structure (bloom OFF)...\n");
    fflush(stdout);

    int32_search_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.use_bloom = 0;

    int32_search_t handle = int32_search_create(data, n, &cfg);
    if (!handle) {
        printf("ERROR: int32_search_create failed\n");
        free(data);
        return 1;
    }

    printf("Generating %zu queries...\n", num_queries);
    fflush(stdout);
    int32_t *queries = (int32_t *)malloc(num_queries * sizeof(int32_t));
    if (!queries) {
        printf("ERROR: malloc failed for queries\n");
        int32_search_destroy(handle);
        free(data);
        return 1;
    }
    for (size_t i = 0; i < num_queries; i++) {
        if ((rand() % 100) < hit_ratio) {
            queries[i] = data[rand() % n];
        } else {
            queries[i] = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
        }
    }

    printf("Running %zu lookups...\n", num_queries);
    fflush(stdout);

    size_t hit_count = 0;
    double t_start = get_time_sec();
    for (size_t i = 0; i < num_queries; i++) {
        size_t idx;
        if (int32_search_find(handle, queries[i], &idx) == INT32_SEARCH_OK) {
            hit_count++;
        }
    }
    double t_end = get_time_sec();

    double total_sec = t_end - t_start;
    double total_ms = total_sec * 1000.0;
    double avg_us = (total_sec / (double)num_queries) * 1000000.0;
    double avg_ns = avg_us * 1000.0;

    printf("\n=== Results ===\n");
    printf("Total queries:      %zu\n", num_queries);
    printf("Hits:               %zu (%.1f%%)\n", hit_count,
           100.0 * hit_count / num_queries);
    printf("Total time:         %.3f ms (%.6f sec)\n", total_ms, total_sec);
    printf("Average per query:  %.2f us (%.0f ns)\n", avg_us, avg_ns);
    printf("Throughput:         %.2f M queries/sec\n",
           num_queries / total_sec / 1000000.0);

    printf("\nDone.\n");

    int32_search_destroy(handle);
    free(queries);
    free(data);
    return 0;
}
