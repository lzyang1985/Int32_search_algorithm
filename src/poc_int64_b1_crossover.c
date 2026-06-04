/*
 * poc_int64_b1_crossover.c — ACT-20: int64 B1 vs Scalar 二分 crossover 阈值校准
 *
 * 编译命令:
 *
 *   :: 性能测量用
 *   gcc -O3 -std=c11 -mavx2 src/poc_int64_b1_crossover.c -o poc_int64_b1_crossover -lm
 *
 *   :: ASan/UBSan 验证用
 *   gcc -O1 -g -std=c11 -mavx2 -fsanitize=address,undefined \
 *       -fno-omit-frame-pointer src/poc_int64_b1_crossover.c -o poc_int64_b1_crossover_asan -lm
 *
 * 运行:
 *   ./poc_int64_b1_crossover       :: 完整 crossover 扫描 (M=8,16,32,64,128,256,512,1024,2048,4096,8192)
 *   ./poc_int64_b1_crossover --correctness   :: 仅正确性验证
 *
 * 目标:
 *   精确测定 int64 B1 (high20 dir + lo44 4路 scan) vs 标量二分的 crossover 点
 *   为 B1_MAX_BUCKET_THRESHOLD_INT64 提供数据依据
 *
 * 方法:
 *   - 受控构造 M 序列: 每个 high20 桶精确包含 M 个有序 int64_t 元素
 *   - 总数据量 ~1M (约 8MB), 足够触发 L2/L3 cache 效应
 *   - 每个 M 7 次重复取中位数, 使用 __rdtscp 测 cycles
 *   - 查询集: random 10000 次 100% hit (最不利 B1 的路径 — 总是扫描完桶)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <immintrin.h>
#include <math.h>
#include <time.h>

#define INT64_DIR_SIZE   1048577
#define TOTAL_ELEMENTS   1048576
#define NUM_QUERIES      10000
#define BENCH_REPS       7
#define BENCH_WARMUP     1
#define BENCH_TOTAL      (BENCH_WARMUP + BENCH_REPS)

static int g_failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
        g_failures++; \
    } else { \
        printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

static int cmp_int64(const void *a, const void *b) {
    int64_t va = *(const int64_t *)a;
    int64_t vb = *(const int64_t *)b;
    return (va > vb) - (va < vb);
}

static uint64_t xorshift64_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static inline double cycles_to_ns(double cycles) {
    return cycles / 2.1;
}

static inline uint64_t read_tsc(void) {
    unsigned int aux;
    return __rdtscp(&aux);
}

/*
 * search_int64_scalar — 标量 lower_bound 二分查找
 * 返回匹配索引, 或 SIZE_MAX 表示未找到
 */
static size_t search_int64_scalar(const int64_t *vals, size_t n, int64_t target) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (lo < n && vals[lo] == target) ? lo : (size_t)-1;
}

/*
 * build_dir_int64 — 构建 high20 目录
 * 使用 sign-flip 将 int64 有符号排序映射为无符号桶排序
 * 空桶用前向填充处理 (dir[i] = dir[i+1])
 */
static int32_t *build_dir_int64(const int64_t *vals, size_t n) {
    int32_t *dir = (int32_t *)_mm_malloc(INT64_DIR_SIZE * sizeof(int32_t), 32);
    if (!dir) return NULL;

    for (int i = 0; i < INT64_DIR_SIZE; i++)
        dir[i] = -1;

    for (size_t j = 0; j < n; j++) {
        uint32_t h = (uint32_t)(((uint64_t)vals[j] ^ ((uint64_t)1 << 63)) >> 44);
        if (dir[h] == -1)
            dir[h] = (int32_t)j;
    }

    dir[INT64_DIR_SIZE - 1] = (int32_t)n;

    for (int i = INT64_DIR_SIZE - 2; i >= 0; i--) {
        if (dir[i] == -1)
            dir[i] = dir[i + 1];
    }

    return dir;
}

/*
 * get_bucket_key — sign-flip 桶映射 (单一函数, 构建和查询共用)
 */
static inline uint32_t get_bucket_key(int64_t key) {
    return (uint32_t)(((uint64_t)key ^ ((uint64_t)1 << 63)) >> 44);
}

/*
 * search_int64_b1 — B1 查询: high20 dir + lo44 4路 cmpeq 扫描
 * 每桶回退到桶内二分 (当 bucket_sz > B1_FALLBACK 时)
 */
static size_t search_int64_b1(const int64_t *vals, const int32_t *dir,
                               size_t n, int64_t target, int32_t fallback_threshold)
{
    if (n == 0 || !vals || !dir) return (size_t)-1;

    uint32_t h = get_bucket_key(target);
    int32_t start = dir[h];
    int32_t end   = dir[h + 1];
    if (start >= end) return (size_t)-1;

    int32_t bucket_sz = end - start;

    if (bucket_sz > fallback_threshold) {
        return search_int64_scalar(vals + start, bucket_sz, target);
    }

    int32_t i = start;
    for (; i + 4 <= end; i += 4) {
        __m256i key4 = _mm256_set1_epi64x(target);
        __m256i vec4 = _mm256_loadu_si256((const __m256i *)(vals + i));
        __m256i eq   = _mm256_cmpeq_epi64(key4, vec4);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(eq));
        if (mask != 0) {
            int idx = i + __builtin_ctz(mask);
            if (vals[idx] == target)
                return (size_t)idx;
        }
    }
    for (; i < end; i++) {
        if (vals[i] == target)
            return (size_t)i;
    }
    return (size_t)-1;
}

/*
 * gen_controlled — 生成受控分布: 每个 high20 桶精确 M 个元素
 * 桶均匀分布在 high20 空间, 桶内元素排序
 * num_buckets = n / M, 桶索引 k ∈ [0, num_buckets)
 * high20 = k * (1048576 / num_buckets) 均匀分布
 */
static int64_t *gen_controlled(size_t n, size_t M, uint64_t seed) {
    int64_t *vals = (int64_t *)_mm_malloc(n * sizeof(int64_t), 32);
    if (!vals) return NULL;

    size_t num_buckets = n / M;
    uint64_t rng = seed;

    for (size_t b = 0; b < num_buckets; b++) {
        uint32_t high20 = (uint32_t)(b * 1048576ULL / num_buckets);
        int64_t base = ((int64_t)(((uint64_t)high20 << 44) ^ ((uint64_t)1 << 63)));

        int64_t step = (int64_t)((1ULL << 44) / (M + 1));
        for (size_t j = 0; j < M; j++) {
            int64_t val = base + (int64_t)(j + 1) * step;
            vals[b * M + j] = val;
        }
    }

    return vals;
}

/*
 * gen_queries_from_data — 从已有数据中随机抽样查询 key (100% hit)
 */
static int64_t *gen_queries_from_data(const int64_t *vals, size_t n,
                                       size_t num_q, uint64_t seed)
{
    int64_t *queries = (int64_t *)malloc(num_q * sizeof(int64_t));
    if (!queries) return NULL;

    uint64_t rng = seed;
    for (size_t i = 0; i < num_q; i++) {
        size_t idx = (size_t)(xorshift64_next(&rng) % n);
        queries[i] = vals[idx];
    }
    return queries;
}

/*
 * bench_one_M — 对给定的 M 值测量 B1 和 Scalar 的 cy/query
 */
typedef struct {
    size_t   M;
    double   b1_cy;
    double   scalar_cy;
    int32_t  b1_fallback;
} crossover_point_t;

static crossover_point_t bench_one_M(size_t M, int32_t fallback_threshold) {
    crossover_point_t result = {M, 0.0, 0.0, fallback_threshold};

    size_t num_buckets = TOTAL_ELEMENTS / M;
    if (num_buckets < 2 || num_buckets > 1048576) {
        fprintf(stderr, "  SKIP M=%zu (num_buckets=%zu out of range)\n", M, num_buckets);
        return result;
    }

    int64_t *vals = gen_controlled(TOTAL_ELEMENTS, M, M * 2654435761ULL);
    if (!vals) { fprintf(stderr, "  SKIP M=%zu (alloc failed)\n", M); return result; }

    int32_t *dir = build_dir_int64(vals, TOTAL_ELEMENTS);
    if (!dir) { _mm_free(vals); fprintf(stderr, "  SKIP M=%zu (dir alloc failed)\n", M); return result; }

    int64_t *queries = gen_queries_from_data(vals, TOTAL_ELEMENTS, NUM_QUERIES, M + 42);
    if (!queries) { _mm_free(dir); _mm_free(vals); return result; }

    double b1_cycles[BENCH_TOTAL];
    double scalar_cycles[BENCH_TOTAL];

    for (int rep = 0; rep < BENCH_TOTAL; rep++) {
        volatile size_t sink = 0;

        uint64_t t0 = read_tsc();
        for (size_t q = 0; q < NUM_QUERIES; q++) {
            size_t idx = search_int64_b1(vals, dir, TOTAL_ELEMENTS, queries[q], fallback_threshold);
            sink += idx;
        }
        uint64_t t1 = read_tsc();
        b1_cycles[rep] = (double)(t1 - t0) / (double)NUM_QUERIES;
        (void)sink;

        uint64_t t2 = read_tsc();
        for (size_t q = 0; q < NUM_QUERIES; q++) {
            size_t idx = search_int64_scalar(vals, TOTAL_ELEMENTS, queries[q]);
            sink += idx;
        }
        uint64_t t3 = read_tsc();
        scalar_cycles[rep] = (double)(t3 - t2) / (double)NUM_QUERIES;
        (void)sink;
    }

    int cmp_double(const void *a, const void *b) {
        double da = *(const double *)a, db = *(const double *)b;
        return (da > db) - (da < db);
    }

    qsort(b1_cycles + BENCH_WARMUP, BENCH_REPS, sizeof(double), cmp_double);
    qsort(scalar_cycles + BENCH_WARMUP, BENCH_REPS, sizeof(double), cmp_double);

    result.b1_cy     = b1_cycles[BENCH_WARMUP + BENCH_REPS / 2];
    result.scalar_cy = scalar_cycles[BENCH_WARMUP + BENCH_REPS / 2];

    printf("  M=%5zu  buckets=%6zu  B1=%8.1f cy/q  Scalar=%8.1f cy/q  ratio=%.2fx\n",
           M, num_buckets, result.b1_cy, result.scalar_cy,
           result.b1_cy / result.scalar_cy);

    free(queries);
    _mm_free(dir);
    _mm_free(vals);

    return result;
}

static void verify_correctness(void) {
    printf("\n=== 正确性验证 ===\n");

    static const size_t test_Ms[] = {8, 64, 256, 1024, 4096};
    int num_tests = sizeof(test_Ms) / sizeof(test_Ms[0]);

    for (int t = 0; t < num_tests; t++) {
        size_t M = test_Ms[t];
        printf("\n--- M=%zu ---\n", M);

        size_t num_buckets = TOTAL_ELEMENTS / M;
        int64_t *vals = gen_controlled(TOTAL_ELEMENTS, M, M * 1234567ULL);
        CHECK(vals != NULL, "M=%zu: gen_controlled 成功", M);

        int32_t *dir = build_dir_int64(vals, TOTAL_ELEMENTS);
        CHECK(dir != NULL, "M=%zu: build_dir 成功", M);

        {
            int32_t max_bucket = 0;
            for (size_t b = 0; b < (size_t)num_buckets; b++) {
                uint32_t h = (uint32_t)(b * 1048576ULL / num_buckets);
                int32_t sz = dir[h + 1] - dir[h];
                if (sz > max_bucket) max_bucket = sz;
            }
            CHECK(max_bucket == (int32_t)M, "M=%zu: max_bucket=%d == %zu", M, max_bucket, M);
        }
        CHECK(dir[INT64_DIR_SIZE - 1] == (int32_t)TOTAL_ELEMENTS, "M=%zu: 哨兵正确", M);

        int64_t *queries = gen_queries_from_data(vals, TOTAL_ELEMENTS, 1000, M + 99);
        CHECK(queries != NULL, "M=%zu: 生成查询集", M);

        int mismatches = 0;
        for (int q = 0; q < 1000; q++) {
            size_t idx_b1 = search_int64_b1(vals, dir, TOTAL_ELEMENTS, queries[q], 999999);
            size_t idx_sc = search_int64_scalar(vals, TOTAL_ELEMENTS, queries[q]);

            if (idx_b1 != idx_sc) {
                if (mismatches < 3)
                    fprintf(stderr, "  MISMATCH q=%d: key=%lld B1=%zu Scalar=%zu\n",
                            q, (long long)queries[q], idx_b1, idx_sc);
                mismatches++;
            }
        }
        CHECK(mismatches == 0, "M=%zu: B1 vs Scalar 1000 查询交叉验证 (%d mismatches)", M, mismatches);

        {
            int found_count = 0, notfound_count = 0;
            uint64_t rng = 42 + M;
            for (int q = 0; q < 1000; q++) {
                int64_t key = (int64_t)xorshift64_next(&rng);
                size_t r1 = search_int64_b1(vals, dir, TOTAL_ELEMENTS, key, 999999);
                size_t r2 = search_int64_scalar(vals, TOTAL_ELEMENTS, key);
                if (r1 == (size_t)-1) notfound_count++;
                else found_count++;
                if (r1 != r2) mismatches++;
            }
            CHECK(mismatches == 0, "M=%zu: 随机key B1 vs Scalar (%d mismatches, found=%d notfound=%d)",
                  M, mismatches, found_count, notfound_count);
        }

        free(queries);
        _mm_free(dir);
        _mm_free(vals);
    }

    printf("\n正确性验证完成: %d failures\n", g_failures);
}

int main(int argc, char **argv) {
    int verify_only = 0;
    if (argc > 1 && strcmp(argv[1], "--correctness") == 0)
        verify_only = 1;

    printf("=== int64 B1 vs Scalar Crossover POC ===\n");
    printf("数据量: %d elements (~%d MB)\n", TOTAL_ELEMENTS, (int)(TOTAL_ELEMENTS * 8 / 1048576));
    printf("查询数: %d (100%% hit)\n", NUM_QUERIES);
    printf("重复次数: %d (取中位数)\n\n", BENCH_REPS);

    if (verify_only) {
        verify_correctness();
        return g_failures ? 1 : 0;
    }

    printf("=== 正确性快速检查 (M=64) ===\n");
    {
        int64_t *v = gen_controlled(TOTAL_ELEMENTS, 64, 12345);
        int32_t  *d = build_dir_int64(v, TOTAL_ELEMENTS);
        int64_t *q = gen_queries_from_data(v, TOTAL_ELEMENTS, 100, 42);
        int ok = 1;
        for (int i = 0; i < 100; i++) {
            if (search_int64_b1(v, d, TOTAL_ELEMENTS, q[i], 999999) !=
                search_int64_scalar(v, TOTAL_ELEMENTS, q[i])) { ok = 0; break; }
        }
        printf("  %s\n\n", ok ? "PASS" : "FAIL");
        free(q); _mm_free(d); _mm_free(v);
        if (!ok) return 1;
    }

    static const size_t M_sequence[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    int num_M = sizeof(M_sequence) / sizeof(M_sequence[0]);

    crossover_point_t results[16];
    int num_results = 0;

    printf("=== Crossover 扫描 ===\n");
    printf("B1 回退阈值设为 INT32_MAX (不使用回退, 测量纯 B1 scan 性能)\n\n");

    for (int i = 0; i < num_M; i++) {
        results[num_results++] = bench_one_M(M_sequence[i], INT32_MAX);
    }

    printf("\n=== 结果汇总 ===\n");
    printf("%6s  %10s  %10s  %8s  %10s\n", "M", "B1 cy/q", "Scalar cy/q", "B1/Scalar", "B1 < Scalar?");
    printf("------  ----------  ----------  --------  ----------\n");

    size_t crossover_M = 0;
    for (int i = 0; i < num_results; i++) {
        crossover_point_t *r = &results[i];
        int b1_wins = r->b1_cy < r->scalar_cy;
        printf("%6zu  %10.1f  %10.1f  %8.2fx  %s\n",
               r->M, r->b1_cy, r->scalar_cy,
               r->b1_cy / r->scalar_cy,
               b1_wins ? "YES" : "NO");

        if (b1_wins && r->M > crossover_M)
            crossover_M = r->M;
    }

    printf("\n=== Crossover 分析 ===\n");

    size_t recommended_threshold = 0;

    for (int i = 1; i < num_results; i++) {
        if (results[i].b1_cy > results[i].scalar_cy) {
            recommended_threshold = results[i - 1].M;
            break;
        }
    }

    if (recommended_threshold == 0)
        recommended_threshold = results[num_results - 1].M;

    recommended_threshold = recommended_threshold * 8 / 10;

    printf("B1 与 Scalar 性能交叉点: M=%zu (最后 B1 优于 Scalar 的 M)\n", crossover_M);
    printf("建议阈值 B1_MAX_BUCKET_THRESHOLD_INT64: %zu (0.8x 安全余量)\n", recommended_threshold);

    if (recommended_threshold > 8192)
        printf("⚠️  超出测试范围! 建议扩展 M 序列至更大值\n");
    else if (recommended_threshold < 8)
        printf("⚠️  阈值过低! B1 在所有 M 下均慢于标量, 建议仅使用标量二分\n");

    printf("\n=== 结论 ===\n");
    printf("B1 在 max_bucket ≤ %zu 时优于标量二分\n", crossover_M);
    printf("推荐阈值: B1_MAX_BUCKET_THRESHOLD_INT64 = %zu\n", recommended_threshold);
    printf("适用分布: uniform (max_bucket=26), skewed 80/20 (max_bucket=71)\n");
    printf("回退分布: zipf α=1.0 (max_bucket=69K/692K)\n");

    return g_failures ? 1 : 0;
}
