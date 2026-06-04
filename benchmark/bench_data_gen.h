#ifndef INT32_SEARCH_BENCH_DATA_GEN_H
#define INT32_SEARCH_BENCH_DATA_GEN_H

#include <stdint.h>
#include <stddef.h>

int32_t *bench_generate_sorted_data(size_t n);
int32_t *bench_generate_queries(const int32_t *data, size_t n,
                                 size_t num_queries, int hit_ratio_percent);
void bench_shuffle(int32_t *arr, size_t n);

#endif
