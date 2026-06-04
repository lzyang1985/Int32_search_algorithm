#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "int64_search.h"

static int compare_int64(const void *a, const void *b)
{
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
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

    printf("=== Int64 Search Demo ===\n");
    printf("Data size:      5,000,000\n");
    printf("Queries:        10,000 (hit rate ~%d%%)\n", hit_ratio);
    printf("\n");

    printf("Generating %zu random int64 numbers...\n", n);
    fflush(stdout);
    int64_t *data = (int64_t *)malloc(n * sizeof(int64_t));
    if (!data) {
        printf("ERROR: malloc failed for data\n");
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        int64_t upper = ((int64_t)rand() << 48) ^ ((int64_t)rand() << 32);
        int64_t lower = ((int64_t)rand() << 16) ^ (int64_t)rand();
        data[i] = upper ^ lower;
    }
    qsort(data, n, sizeof(int64_t), compare_int64);

    printf("Building search structure...\n");
    fflush(stdout);
    int64_search_t handle = int64_search_create(data, n, NULL);
    if (!handle) {
        printf("ERROR: int64_search_create failed\n");
        free(data);
        return 1;
    }

    printf("Generating %zu queries...\n", num_queries);
    fflush(stdout);
    int64_t *queries = (int64_t *)malloc(num_queries * sizeof(int64_t));
    if (!queries) {
        printf("ERROR: malloc failed for queries\n");
        int64_search_destroy(handle);
        free(data);
        return 1;
    }
    for (size_t i = 0; i < num_queries; i++) {
        if ((rand() % 100) < hit_ratio) {
            queries[i] = data[rand() % n];
        } else {
            int64_t upper = ((int64_t)rand() << 48) ^ ((int64_t)rand() << 32);
            int64_t lower = ((int64_t)rand() << 16) ^ (int64_t)rand();
            queries[i] = upper ^ lower;
        }
    }

    printf("Running %zu lookups...\n", num_queries);
    fflush(stdout);

    size_t hit_count = 0;
    double t_start = get_time_sec();
    for (size_t i = 0; i < num_queries; i++) {
        size_t idx;
        if (int64_search_find(handle, queries[i], &idx) == INT64_SEARCH_OK) {
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

    int64_search_destroy(handle);
    free(queries);
    free(data);
    return 0;
}
