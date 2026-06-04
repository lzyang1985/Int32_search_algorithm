#include "bench_data_gen.h"
#include <stdlib.h>
#include <string.h>

static int compare_int32(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

int32_t *bench_generate_sorted_data(size_t n)
{
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    if (data == NULL) return NULL;

    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)((unsigned int)rand() ^ ((unsigned int)rand() << 15));
    qsort(data, n, sizeof(int32_t), compare_int32);
    return data;
}

void bench_shuffle(int32_t *arr, size_t n)
{
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = (size_t)rand() % (i + 1);
        int32_t tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

int32_t *bench_generate_queries(const int32_t *data, size_t n,
                                 size_t num_queries, int hit_ratio_percent)
{
    int32_t *queries = (int32_t *)malloc(num_queries * sizeof(int32_t));
    if (queries == NULL) return NULL;

    size_t hit_count = (size_t)((unsigned long long)num_queries * hit_ratio_percent / 100);

    for (size_t i = 0; i < hit_count; i++) {
        size_t idx = (size_t)rand() % n;
        queries[i] = data[idx];
    }

    for (size_t i = hit_count; i < num_queries; i++) {
        int32_t candidate;
        int tries = 0;
        do {
            candidate = (int32_t)((unsigned int)rand() ^ ((unsigned int)rand() << 15));
        } while (bsearch(&candidate, data, n, sizeof(int32_t), compare_int32) != NULL && ++tries < 100);
        queries[i] = candidate;
    }

    bench_shuffle(queries, num_queries);
    return queries;
}
