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

#define B 16

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

typedef struct {
    int32_t keys[B - 1];
    int32_t children[B];
    int num_keys;
    int is_leaf;
} btree_node_t;

static btree_node_t *build_btree(const int32_t *sorted, size_t n, size_t *num_nodes_out)
{
    if (n == 0) { *num_nodes_out = 0; return NULL; }

    size_t leaves = (n + (B - 1) - 1) / (B - 1);
    size_t max_nodes = leaves * 2;
    btree_node_t *nodes = (btree_node_t *)calloc(max_nodes, sizeof(btree_node_t));
    if (!nodes) { *num_nodes_out = 0; return NULL; }
    size_t node_count = 0;

    for (size_t i = 0; i < n; i += (B - 1)) {
        btree_node_t *leaf = &nodes[node_count++];
        leaf->is_leaf = 1;
        size_t chunk = (n - i) < (size_t)(B - 1) ? (n - i) : (size_t)(B - 1);
        leaf->num_keys = (int)chunk;
        memcpy(leaf->keys, sorted + i, chunk * sizeof(int32_t));
    }

    size_t leaf_start = 0;
    size_t leaf_count = node_count;

    while (leaf_count > 1) {
        size_t internal_start = node_count;
        for (size_t i = 0; i < leaf_count; i += B) {
            btree_node_t *internal = &nodes[node_count++];
            internal->is_leaf = 0;
            size_t children_in_group = (leaf_count - i) < (size_t)B ? (leaf_count - i) : (size_t)B;
            internal->num_keys = (int)(children_in_group - 1);
            for (size_t j = 0; j < (size_t)internal->num_keys; j++) {
                btree_node_t *child = &nodes[leaf_start + i + j + 1];
                while (!child->is_leaf) {
                    child = &nodes[child->children[0]];
                }
                internal->keys[j] = child->keys[0];
            }
            for (size_t j = 0; j < children_in_group; j++)
                internal->children[j] = (int32_t)(leaf_start + i + j);
        }
        leaf_start = internal_start;
        leaf_count = node_count - internal_start;
    }

    *num_nodes_out = node_count;
    return nodes;
}

static int32_t search_btree_simd(btree_node_t *nodes, size_t root_idx, int32_t target)
{
    int32_t current = (int32_t)root_idx;
    while (current >= 0) {
        btree_node_t *node = &nodes[current];

        if (node->is_leaf) {
            for (int k = 0; k < node->num_keys; k++)
                if (node->keys[k] == target) return 0;
            return -1;
        }

        int child_idx = node->num_keys;
        __m256i key_vec = _mm256_set1_epi32(target);

        for (int k = 0; k < node->num_keys; k += 8) {
            int batch = (node->num_keys - k) < 8 ? (node->num_keys - k) : 8;
            __m256i keys_vec = _mm256_loadu_si256((const __m256i *)(node->keys + k));
            __m256i cmp = _mm256_cmpgt_epi32(keys_vec, key_vec);
            int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
            int mask_low = mask & ((1 << batch) - 1);

            if (mask_low != 0) {
                child_idx = k + __builtin_ctz((unsigned int)mask_low);
                break;
            }
        }

        if (child_idx > 0 && node->keys[child_idx - 1] == target)
            return 0;

        current = node->children[child_idx];
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

static double bench_btree(btree_node_t *nodes, size_t root_idx,
                           const int32_t *queries, size_t num_queries, int *out_hits)
{
    volatile int discard = 0;
    for (size_t i = 0; i < 100; i++)
        discard |= search_btree_simd(nodes, root_idx, queries[i]);

    int hits = 0;
    unsigned long long t0 = rdtscp_bench();
    for (size_t q = 0; q < num_queries; q++) {
        int r = search_btree_simd(nodes, root_idx, queries[q]);
        if (r == 0) hits++;
        discard += r;
    }
    unsigned long long t1 = rdtscp_bench();
    (void)discard;
    *out_hits = hits;
    return (double)(t1 - t0) / (double)num_queries;
}

static double bench_scalar(const int32_t *vals, size_t n,
                            const int32_t *queries, size_t num_queries, int *out_hits)
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

int main(void)
{
    srand((unsigned int)time(NULL));
    printf("B-Tree (S-tree) with SIMD Search POC\n");
    printf("=====================================\n");
    printf("Compiler: GCC %d.%d.%d, B=%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__, B);
    printf("\n");

    size_t sizes[] = {1000000, 10000000};
    int n_sizes = 2;

    for (int si = 0; si < n_sizes; si++) {
        size_t n = sizes[si];
        size_t num_queries = 1000000;

        srand(12345 + si);
        int32_t *data = generate_sorted_data(n);
        int32_t *queries = generate_queries(data, n, num_queries);

        size_t num_nodes = 0;
        btree_node_t *nodes = build_btree(data, n, &num_nodes);
        if (!nodes) {
            printf("--- N = %zu — OOM ---\n", n);
            free(data); free(queries);
            continue;
        }
        size_t root_idx = num_nodes - 1;

        double ram_mb = (double)(num_nodes * sizeof(btree_node_t)) / (1024.0 * 1024.0);

        printf("--- N = %zu (%.1fM), Nodes=%zu, RAM=%.0f MB ---\n",
               n, n/1000000.0, num_nodes, ram_mb);

        int b_hits, s_hits;
        double b_cy = bench_btree(nodes, root_idx, queries, num_queries, &b_hits);
        double s_cy = bench_scalar(data, n, queries, num_queries, &s_hits);

        printf("  B-Tree SIMD: %8.1f cy/q  (hits=%d)\n", b_cy, b_hits);
        printf("  Scalar BS:   %8.1f cy/q  (hits=%d)\n", s_cy, s_hits);
        printf("  Speedup:     %.2fx vs scalar\n", s_cy / b_cy);

        free(nodes);
        free(data);
        free(queries);
    }

    printf("\nDone.\n");
    return 0;
}
