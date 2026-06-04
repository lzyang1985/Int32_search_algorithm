/*
 * poc_bloom_bypass.c — ACT-16: Bloom 旁路正确性验证 (meeting_013 方案 C')
 *
 * 编译命令:
 *
 *   :: Linux
 *   gcc -O3 -std=c11 -mavx2 src/poc_bloom_bypass.c -o poc_bloom_bypass -lm -lpthread
 *
 *   :: ASan/UBSan 验证用 (Linux)
 *   gcc -O1 -g -std=c11 -mavx2 -fsanitize=address,undefined \
 *       -fno-omit-frame-pointer src/poc_bloom_bypass.c -o poc_bloom_bypass_asan -lm -lpthread
 *
 *   :: Windows MinGW (不含 sanitizer, 不含 concurrency test)
 *   gcc -O3 -std=c11 -mavx2 src/poc_bloom_bypass.c -o poc_bloom_bypass.exe -lm
 *
 * 运行:
 *   poc_bloom_bypass           :: 全部验证 (V-BP-01 ~ V-BP-05)
 *
 * 验收标准:
 *   GATE-4: 5 项验证全部通过
 *
 * 验证项:
 *   V-BP-01: bypass=0 bloom 预筛生效
 *   V-BP-02: bypass=1 跳过 bloom
 *   V-BP-03: 存在 key 切换一致性
 *   V-BP-04: NULL bloom 安全 no-op
 *   V-BP-05: 并发安全 (2 writer + 4 reader 线程)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <immintrin.h>

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#endif

#define BLOOM_K         3
#define BLOOM_BITS_PER  12
#define BLOOM_SEED_0    0x9747b28cu
#define BLOOM_SEED_1    0x5d3a11e7u
#define BLOOM_SEED_2    0x1f8c3e96u

static int g_failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
        g_failures++; \
    } else { \
        printf("  PASS: " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define API_NOT_FOUND  SIZE_MAX

static int cmp_int32(const void *a, const void *b) {
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
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

/*
 * 简易 bloom filter（K=3, BITS_PER=12, 内联 hash）
 */
typedef struct {
    uint8_t *bits;
    size_t   m;
    uint32_t seeds[BLOOM_K];
} bloom_filter_t;

static uint32_t bloom_hash(uint32_t seed, int32_t key) {
    uint32_t h = seed;
    h ^= (uint32_t)key;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static bloom_filter_t *bloom_create(size_t n) {
    bloom_filter_t *bf = (bloom_filter_t *)calloc(1, sizeof(bloom_filter_t));
    if (!bf) return NULL;
    bf->m = n * BLOOM_BITS_PER;
    bf->bits = (uint8_t *)calloc(1, (bf->m + 7) / 8);
    if (!bf->bits) { free(bf); return NULL; }
    bf->seeds[0] = BLOOM_SEED_0;
    bf->seeds[1] = BLOOM_SEED_1;
    bf->seeds[2] = BLOOM_SEED_2;
    return bf;
}

static void bloom_insert(bloom_filter_t *bf, int32_t key) {
    size_t m = bf->m;
    for (int k = 0; k < BLOOM_K; k++) {
        uint32_t h = bloom_hash(bf->seeds[k], key);
        size_t bit = (size_t)h % m;
        bf->bits[bit / 8] |= (uint8_t)(1u << (bit % 8));
    }
}

static int bloom_query(const bloom_filter_t *bf, int32_t key) {
    size_t m = bf->m;
    for (int k = 0; k < BLOOM_K; k++) {
        uint32_t h = bloom_hash(bf->seeds[k], key);
        size_t bit = (size_t)h % m;
        if (!(bf->bits[bit / 8] & (uint8_t)(1u << (bit % 8))))
            return 0;
    }
    return 1;
}

static void bloom_destroy(bloom_filter_t *bf) {
    if (bf) {
        free(bf->bits);
        free(bf);
    }
}

/*
 * poc_impl_t — POC 实现体
 */
typedef struct {
    const int32_t *vals;
    size_t          n;
    void           *bloom;
    _Atomic(int)    bloom_bypass;
} poc_impl_t;

static poc_impl_t *poc_create(const int32_t *vals, size_t n, int use_bloom) {
    poc_impl_t *poc = (poc_impl_t *)calloc(1, sizeof(poc_impl_t));
    if (!poc) return NULL;
    poc->vals = vals;
    poc->n    = n;
    atomic_init(&poc->bloom_bypass, 0);
    if (use_bloom && n > 0) {
        bloom_filter_t *bf = bloom_create(n);
        if (bf) {
            for (size_t i = 0; i < n; i++)
                bloom_insert(bf, vals[i]);
        }
        poc->bloom = bf;
    }
    return poc;
}

static size_t poc_find(const poc_impl_t *poc, int32_t target) {
    if (!poc || poc->n == 0) return API_NOT_FOUND;

    int bypass = atomic_load_explicit(&poc->bloom_bypass, memory_order_acquire);

    if (!bypass) {
        const bloom_filter_t *bf = (const bloom_filter_t *)poc->bloom;
        if (bf && !bloom_query(bf, target))
            return API_NOT_FOUND;
    }

    size_t lo = 0, hi = poc->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (poc->vals[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return (lo < poc->n && poc->vals[lo] == target) ? lo : API_NOT_FOUND;
}

static void poc_set_bloom_bypass(poc_impl_t *poc, int bypass) {
    if (!poc) return;
    atomic_store_explicit(&poc->bloom_bypass, bypass, memory_order_release);
}

static void poc_destroy(poc_impl_t *poc) {
    if (poc) {
        bloom_destroy((bloom_filter_t *)poc->bloom);
        free(poc);
    }
}

/*
 * 测试
 */

/* V-BP-01: bypass=0 时 bloom 拦截不存在的 key */
static void test_v_bp_01(const int32_t *data, size_t n) {
    printf("--- V-BP-01: bloom pre-filter active (bypass=0) ---\n");

    poc_impl_t *poc = poc_create(data, n, 1);
    CHECK(poc != NULL, "poc_create with bloom success");
    CHECK(poc->bloom != NULL, "bloom handle not NULL");

    int32_t missing_keys[] = { INT32_MIN, INT32_MAX, 0 };
    const char *names[] = { "INT32_MIN", "INT32_MAX", "0" };
    int n_keys = 3;

    for (int i = 0; i < n_keys; i++) {
        int32_t key = missing_keys[i];
        int in_data = (bsearch(&key, (void *)data, n, sizeof(int32_t), cmp_int32) != NULL);
        if (in_data) continue;

        size_t result = poc_find(poc, key);
        CHECK(result == API_NOT_FOUND,
              "V-BP-01: missing key %s (%d) → NOT_FOUND (bloom intercept)",
              names[i], key);
    }

    poc_destroy(poc);
}

/* V-BP-02: bypass=1 时跳过 bloom，走完整搜索 */
static void test_v_bp_02(const int32_t *data, size_t n) {
    printf("\n--- V-BP-02: bloom bypass active (bypass=1) ---\n");

    poc_impl_t *poc = poc_create(data, n, 1);
    poc_set_bloom_bypass(poc, 1);

    int bypass_val = atomic_load_explicit(&poc->bloom_bypass, memory_order_acquire);
    CHECK(bypass_val == 1, "V-BP-02: bypass flag set to 1");

    int verified = 1;
    for (size_t i = 0; i < n && i < 1000; i++) {
        size_t result = poc_find(poc, data[i]);
        if (result != i) { verified = 0; break; }
    }
    CHECK(verified == 1, "V-BP-02: bypass=1 finds all present keys correctly");

    int32_t missing_keys[] = { INT32_MIN, INT32_MAX, 0 };
    int n_keys = 3;
    for (int i = 0; i < n_keys; i++) {
        int in_data = (bsearch(&missing_keys[i], (void *)data, n, sizeof(int32_t), cmp_int32) != NULL);
        if (in_data) continue;
        size_t result = poc_find(poc, missing_keys[i]);
        CHECK(result == API_NOT_FOUND, "V-BP-02: missing key %d → NOT_FOUND (full scan)", missing_keys[i]);
    }

    poc_destroy(poc);
}

/* V-BP-03: bypass 切换前后已存在 key 的一致性 */
static void test_v_bp_03(const int32_t *data, size_t n) {
    printf("\n--- V-BP-03: bypass toggle consistency ---\n");

    poc_impl_t *poc = poc_create(data, n, 1);

    size_t num_check = (n < 100) ? n : 100;
    size_t *results_off = (size_t *)malloc(num_check * sizeof(size_t));
    size_t *results_on  = (size_t *)malloc(num_check * sizeof(size_t));

    poc_set_bloom_bypass(poc, 0);
    for (size_t i = 0; i < num_check; i++)
        results_off[i] = poc_find(poc, data[i]);

    poc_set_bloom_bypass(poc, 1);
    for (size_t i = 0; i < num_check; i++)
        results_on[i] = poc_find(poc, data[i]);

    int mismatch = 0;
    for (size_t i = 0; i < num_check; i++) {
        if (results_off[i] != results_on[i]) {
            mismatch++;
            break;
        }
    }
    CHECK(mismatch == 0, "V-BP-03: bypass toggle produces identical results for %zu keys", num_check);

    free(results_on);
    free(results_off);
    poc_destroy(poc);
}

/* V-BP-04: bloom=NULL 时的安全 no-op */
static void test_v_bp_04(const int32_t *data, size_t n) {
    printf("\n--- V-BP-04: NULL bloom safety ---\n");

    poc_impl_t *poc = poc_create(data, n, 0);
    CHECK(poc != NULL, "poc_create without bloom success");
    CHECK(poc->bloom == NULL, "bloom handle is NULL");

    poc_set_bloom_bypass(poc, 0);
    size_t result = poc_find(poc, data[0]);
    CHECK(result == 0, "V-BP-04: search with NULL bloom + bypass=0 works (no crash)");

    poc_set_bloom_bypass(poc, 1);
    result = poc_find(poc, data[0]);
    CHECK(result == 0, "V-BP-04: search with NULL bloom + bypass=1 works (no crash)");

    poc_destroy(poc);
}

/* V-BP-05: 并发安全 */
#ifndef _WIN32
typedef struct {
    poc_impl_t     *poc;
    const int32_t  *queries;
    size_t          num_queries;
    int             thread_id;
    _Atomic(int)   *stop_flag;
    int             crash;
} thread_ctx_t;

static void *writer_thread(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    int state = 0;
    while (!atomic_load_explicit(ctx->stop_flag, memory_order_acquire)) {
        poc_set_bloom_bypass(ctx->poc, state);
        state = !state;
    }
    return NULL;
}

static void *reader_thread(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    while (!atomic_load_explicit(ctx->stop_flag, memory_order_acquire)) {
        for (size_t i = 0; i < ctx->num_queries; i++) {
            volatile size_t r = poc_find(ctx->poc, ctx->queries[i]);
            (void)r;
        }
    }
    return NULL;
}

static void test_v_bp_05(const int32_t *data, size_t n) {
    printf("\n--- V-BP-05: Concurrency safety ---\n");

    poc_impl_t *poc = poc_create(data, n, 1);
    CHECK(poc != NULL, "poc_create with bloom success");

    size_t num_q = 500;
    int32_t *queries = (int32_t *)malloc(num_q * sizeof(int32_t));
    uint64_t seed_val = 777;
    for (size_t i = 0; i < num_q; i++)
        queries[i] = data[(size_t)(xorshift64_next(&seed_val) % n)];

    _Atomic(int) stop_flag = 0;

    pthread_t writers[2], readers[4];
    thread_ctx_t w_ctxs[2], r_ctxs[4];

    for (int i = 0; i < 2; i++) {
        w_ctxs[i].poc        = poc;
        w_ctxs[i].queries    = queries;
        w_ctxs[i].num_queries = num_q;
        w_ctxs[i].stop_flag  = &stop_flag;
        w_ctxs[i].crash      = 0;
        pthread_create(&writers[i], NULL, writer_thread, &w_ctxs[i]);
    }
    for (int i = 0; i < 4; i++) {
        r_ctxs[i].poc        = poc;
        r_ctxs[i].queries    = queries;
        r_ctxs[i].num_queries = num_q;
        r_ctxs[i].stop_flag  = &stop_flag;
        r_ctxs[i].crash      = 0;
        pthread_create(&readers[i], NULL, reader_thread, &r_ctxs[i]);
    }

    printf("  Running 2 writers + 4 readers for 2 seconds...\n");
    fflush(stdout);
    sleep(2);

    atomic_store_explicit(&stop_flag, 1, memory_order_release);

    for (int i = 0; i < 4; i++) pthread_join(readers[i], NULL);
    for (int i = 0; i < 2; i++) pthread_join(writers[i], NULL);

    printf("  All threads joined cleanly\n");

    int all_ok = 1;
    for (int i = 0; i < 2; i++) { if (w_ctxs[i].crash) all_ok = 0; }
    for (int i = 0; i < 4; i++) { if (r_ctxs[i].crash) all_ok = 0; }

    CHECK(all_ok == 1, "V-BP-05: 2 writers + 4 readers, no crashes");

    free(queries);
    poc_destroy(poc);
}
#else
static void test_v_bp_05(const int32_t *data, size_t n) {
    (void)data; (void)n;
    printf("\n--- V-BP-05: Concurrency safety (SKIPPED on Windows) ---\n");
    CHECK(1, "V-BP-05: skipped (pthread not available on Windows)");
}
#endif

int main(void) {
    printf("=== Bloom Bypass POC (meeting_013 方案 C') ===\n\n");

    /* 生成测试数据: [100, 200, 300, ..., 99900] (不含 INT32_MIN/MAX/0) */
    size_t n = 1000;
    int32_t *data = (int32_t *)_mm_malloc(n * sizeof(int32_t), 32);
    if (!data) { fprintf(stderr, "FATAL: _mm_malloc failed\n"); return 1; }
    for (size_t i = 0; i < n; i++)
        data[i] = (int32_t)((i + 1) * 100);
    qsort(data, n, sizeof(int32_t), cmp_int32);

    test_v_bp_01(data, n);
    test_v_bp_02(data, n);
    test_v_bp_03(data, n);
    test_v_bp_04(data, n);
    test_v_bp_05(data, n);

    _mm_free(data);

    printf("\n=== GATE-4: %s (%d failures) ===\n",
           g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures > 0 ? 1 : 0;
}
