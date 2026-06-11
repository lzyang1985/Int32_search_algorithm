/* ============================================================================
 * test_b1_dir_fuzz.c — Dir Fuzz 基础覆盖率 (ACT-62)
 *
 * 注入 start/end 异常值对到 Int32 和 Int64 的 dir 数组，
 * 验证 search_b1_find / search_int64_b1 在任何输入下都零崩溃。
 *
 * 编译 (Linux):
 *   gcc -O3 -std=c11 -mavx2 -Iinclude -Isrc test/test_b1_dir_fuzz.c \
 *       libint32search.a libint64search.a -o b1_dir_fuzz -lm
 *
 * 验收标准 (SG-3):
 *   所有异常值对注入后零崩溃, 函数仅返回 NOT_FOUND / -1 / ERR 或正常查找结果.
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Int32 内部接口 ── */
#include "internal.h"
#include "search_b1.h"

#define INT32_DIR_ENTRIES 65536
#define INT32_DIR_SIZE    (INT32_DIR_ENTRIES + 1)

/* ── Int64 内部接口 ── */
#include "internal_int64.h"

extern size_t search_int64_b1(const int64_t *vals, const int32_t *dir,
                               size_t n, int64_t target);

/* ── 辅助宏 ── */
static int g_failures = 0;

#define TEST(name, body) do {                           \
    printf("  %-55s", name);                            \
    fflush(stdout);                                     \
    int _prev = g_failures;                             \
    body;                                               \
    if (g_failures == _prev) printf("PASS\n");          \
} while(0)

#define CHECK(cond, msg) do {                           \
    if (!(cond)) {                                      \
        printf("FAIL: %s\n", msg);                      \
        g_failures++;                                   \
    }                                                   \
} while(0)

#define CRASH_TEST(name, body) do {                     \
    printf("  %-55s", name);                            \
    fflush(stdout);                                     \
    int _prev = g_failures;                             \
    body;                                               \
    if (g_failures == _prev) printf("PASS\n");          \
} while(0)

/* ======================================================================== */

static int cmp_int32(const void *a, const void *b) {
    int32_t ia = *(const int32_t *)a;
    int32_t ib = *(const int32_t *)b;
    return (ia > ib) - (ia < ib);
}

static int cmp_int64(const void *a, const void *b) {
    int64_t ia = *(const int64_t *)a;
    int64_t ib = *(const int64_t *)b;
    return (ia > ib) - (ia < ib);
}

/* ── 构造 Int32 lo16 数组 ── */
static uint16_t *build_lo16_int32(const int32_t *vals, size_t n) {
    uint16_t *lo16 = (uint16_t *)malloc(n * sizeof(uint16_t));
    if (!lo16) return NULL;
    for (size_t i = 0; i < n; i++)
        lo16[i] = (uint16_t)(vals[i] & 0xFFFF);
    return lo16;
}

/* ── 构造 Int32 dir (简化版, 用于 fuzz) ── */
static int32_t *build_dir_int32_fuzz(const int32_t *vals, size_t n) {
    int32_t *dir = (int32_t *)calloc(INT32_DIR_SIZE, sizeof(int32_t));
    if (!dir) return NULL;
    for (int i = 0; i < INT32_DIR_SIZE; i++) dir[i] = -1;

    for (size_t i = 0; i < n; i++) {
        uint16_t h = (uint16_t)((uint32_t)vals[i] >> 16) ^ 0x8000;
        if (dir[h] == -1) dir[h] = (int32_t)i;
    }

    int32_t next = (int32_t)n;
    for (int i = INT32_DIR_ENTRIES - 1; i >= 0; i--) {
        if (dir[i] == -1) dir[i] = next;
        else next = dir[i];
    }
    dir[INT32_DIR_ENTRIES] = (int32_t)n;
    return dir;
}

/* ── 构造 Int64 dir (简化版, 用于 fuzz) ── */
static int32_t *build_dir_int64_fuzz(const int64_t *vals, size_t n) {
    int32_t *dir = (int32_t *)calloc(INT64_DIR_SIZE, sizeof(int32_t));
    if (!dir) return NULL;
    for (int i = 0; i < INT64_DIR_SIZE; i++) dir[i] = -1;

    for (size_t j = 0; j < n; j++) {
        uint32_t h = get_bucket_key(vals[j]);
        if (dir[h] == -1) dir[h] = (int32_t)j;
    }

    dir[INT64_DIR_ENTRIES] = (int32_t)n;
    for (int i = INT64_DIR_ENTRIES - 1; i >= 0; i--) {
        if (dir[i] == -1) dir[i] = dir[i + 1];
    }
    return dir;
}

/* ========================================================================
 * 异常值对定义
 *
 * 每个异常值对描述: { start 修改量, end 修改量, 描述 }
 *   0  = 不修改 (保持原始值)
 *   -1 = 设为 -1
 *   -2 = 设为 INT32_MIN
 *   N  = 设为 n + 偏移
 *   MAX= 设为 INT32_MAX
 * ======================================================================== */

typedef struct {
    const char *desc;
    int         start_val;   /* 直接设置 start 的值 */
    int         end_val;     /* 直接设置 end 的值 */
} anomaly_t;

/* 对 DIR 的第 h 个桶 (start=dir[h], end=dir[h+1] or sentinel) 注入异常.
 * 这会同时修改 dir[h] 和 dir[h+1], 后续测试时不再依赖正常的 dir[h+1].
 * 因此每个异常值对需要一个独立的 dir 副本. */

static void inject_anomaly(int32_t *dir, int h, int start_val, int end_val,
                           size_t n, int is_int64)
{
    (void)n;
    int sentinel_idx = is_int64 ? INT64_DIR_ENTRIES : INT32_DIR_ENTRIES;

    if (start_val != 0) {
        /* 0 表示不修改, 保留原值 */
        dir[h] = start_val;
    }
    if (end_val != 0) {
        int ei = (h < sentinel_idx - 1) ? (h + 1) : sentinel_idx;
        dir[ei] = end_val;
    }
}

/* ========================================================================
 * Int32 Dir Fuzz
 * ======================================================================== */

/* 异常值对: 直接设定 start/end 的具体值.
 *  0 = 保留不修改
 *  N  = 设为 n (哨兵值)
 *  N+1 = 设为 n+1 (越界)
 *  MAX = INT32_MAX
 *  MIN = INT32_MIN */
#define SET(x) x

static void run_int32_fuzz(void)
{
    printf("\n=== Int32 Dir Fuzz ===\n");

    const size_t N = 5000;
    int32_t *vals = (int32_t *)malloc(N * sizeof(int32_t));
    for (size_t i = 0; i < N; i++)
        vals[i] = (int32_t)((i * 7919 + 1) % 2000000000 - 1000000000);
    qsort(vals, N, sizeof(int32_t), cmp_int32);

    int32_t *golden_dir = build_dir_int32_fuzz(vals, N);
    uint16_t *lo16      = build_lo16_int32(vals, N);

    /* 选几个 bucket 做测试 */
    int test_buckets[] = { 0, 100, 10000, 30000, 50000, 65534, 65535 };
    int nbuckets = sizeof(test_buckets) / sizeof(test_buckets[0]);

    int32_t targets[] = { vals[0], vals[N/2], vals[N-1], 0, -1, INT32_MAX, INT32_MIN };
    int ntargets = sizeof(targets) / sizeof(targets[0]);

    /* ── 异常值对枚举 ── */
    struct { const char *desc; int s; int e; } anomalies[] = {
        {"start=-1, end=sentinel",    -1,        0           },
        {"start=-100, end=sentinel",  -100,      0           },
        {"start=INT32_MIN, end=N",    INT32_MIN, (int)N     },
        {"start=N+10, end=sentinel",  (int)N+10, 0           },
        {"start=INT32_MAX, end=N",    INT32_MAX, (int)N     },
        {"start=0, end=-1",           0,         -1          },
        {"start=0, end=-100",         0,         -100        },
        {"start=0, end=INT32_MIN",    0,         INT32_MIN   },
        {"start=0, end=N+100",        0,         (int)N+100  },
        {"start=0, end=INT32_MAX",    0,         INT32_MAX   },
        {"start=5, end=3 (inverted)", 5,         3           },
        {"start=N, end=N (empty)",    (int)N,    (int)N      },
        {"start=-1, end=-1",          -1,        -1          },
        {"start=-1, end=INT32_MAX",   -1,        INT32_MAX   },
        {"start=N, end=INT32_MAX",    (int)N,    INT32_MAX   },
    };
    int nanomalies = sizeof(anomalies) / sizeof(anomalies[0]);

    for (int ai = 0; ai < nanomalies; ai++) {
        for (int bi = 0; bi < nbuckets; bi++) {
            int h = test_buckets[bi];
            if (h >= INT32_DIR_ENTRIES) continue;

            char testname[128];
            snprintf(testname, sizeof(testname),
                     "Int32 bucket=%d %s", h, anomalies[ai].desc);

            CRASH_TEST(testname, {
                /* 每个测试用独立的 dir 副本 */
                int32_t *dir = (int32_t *)malloc(INT32_DIR_SIZE * sizeof(int32_t));
                memcpy(dir, golden_dir, INT32_DIR_SIZE * sizeof(int32_t));

                inject_anomaly(dir, h, anomalies[ai].s, anomalies[ai].e, N, 0);

                /* 调用 search_b1_find, 期望不崩溃 */
                for (int ti = 0; ti < ntargets; ti++) {
                    size_t out_idx;
                    /* volatile 防止编译器优化掉调用 */
                    volatile int rc = search_b1_find(
                        vals, lo16, dir, N, targets[ti], &out_idx);
                    (void)rc;
                    (void)out_idx;
                }

                free(dir);
            });
        }
    }

    free(golden_dir);
    free(lo16);
    free(vals);
}

/* ========================================================================
 * Int64 Dir Fuzz
 * ======================================================================== */

static void run_int64_fuzz(void)
{
    printf("\n=== Int64 Dir Fuzz ===\n");

    const size_t N = 5000;
    int64_t *vals = (int64_t *)malloc(N * sizeof(int64_t));
    for (size_t i = 0; i < N; i++)
        vals[i] = (int64_t)((i * 7919 + 1) % 2000000000 - 1000000000);
    qsort(vals, N, sizeof(int64_t), cmp_int64);

    int32_t *golden_dir = build_dir_int64_fuzz(vals, N);

    /* 选几个 bucket 做测试 */
    int test_buckets[] = { 0, 100, 10000, 100000, 500000, 1048574, 1048575 };
    int nbuckets = sizeof(test_buckets) / sizeof(test_buckets[0]);

    int64_t targets[] = { vals[0], vals[N/2], vals[N-1], 0, -1, INT64_MAX, INT64_MIN };
    int ntargets = sizeof(targets) / sizeof(targets[0]);

    struct { const char *desc; int s; int e; } anomalies[] = {
        {"start=-1, end=sentinel",       -1,            0               },
        {"start=-100, end=sentinel",     -100,          0               },
        {"start=INT32_MIN, end=N",       INT32_MIN,     (int)N          },
        {"start=N+10, end=sentinel",     (int)N+10,     0               },
        {"start=INT32_MAX, end=N",       INT32_MAX,     (int)N          },
        {"start=0, end=-1",              0,             -1              },
        {"start=0, end=-100",            0,             -100            },
        {"start=0, end=INT32_MIN",       0,             INT32_MIN       },
        {"start=0, end=N+100",           0,             (int)N+100      },
        {"start=0, end=INT32_MAX",       0,             INT32_MAX       },
        {"start=5, end=3 (inverted)",    5,             3               },
        {"start=N, end=N (empty)",       (int)N,        (int)N          },
        {"start=-1, end=-1",             -1,            -1              },
        {"start=-1, end=INT32_MAX",      -1,            INT32_MAX       },
        {"start=N, end=INT32_MAX",       (int)N,        INT32_MAX       },
    };
    int nanomalies = sizeof(anomalies) / sizeof(anomalies[0]);

    for (int ai = 0; ai < nanomalies; ai++) {
        for (int bi = 0; bi < nbuckets; bi++) {
            int h = test_buckets[bi];
            if (h >= INT64_DIR_ENTRIES) continue;

            char testname[128];
            snprintf(testname, sizeof(testname),
                     "Int64 bucket=%d %s", h, anomalies[ai].desc);

            CRASH_TEST(testname, {
                int32_t *dir = (int32_t *)malloc(INT64_DIR_SIZE * sizeof(int32_t));
                memcpy(dir, golden_dir, INT64_DIR_SIZE * sizeof(int32_t));

                inject_anomaly(dir, h, anomalies[ai].s, anomalies[ai].e, N, 1);

                for (int ti = 0; ti < ntargets; ti++) {
                    volatile size_t rc = search_int64_b1(
                        vals, dir, N, targets[ti]);
                    (void)rc;
                }

                free(dir);
            });
        }
    }

    free(golden_dir);
    free(vals);
}

/* ========================================================================
 * Sanity: 正常 dir 不应该有异常行为
 * ======================================================================== */

static void run_sanity_int32(void)
{
    printf("\n=== Sanity: Int32 normal dir ===\n");

    const size_t N = 5000;
    int32_t *vals = (int32_t *)malloc(N * sizeof(int32_t));
    for (size_t i = 0; i < N; i++)
        vals[i] = (int32_t)((i * 7919 + 1) % 2000000000 - 1000000000);
    qsort(vals, N, sizeof(int32_t), cmp_int32);

    int32_t *dir    = build_dir_int32_fuzz(vals, N);
    uint16_t *lo16  = build_lo16_int32(vals, N);

    TEST("Int32 normal: all hits correct", {
        for (size_t i = 0; i < N; i += 500) {
            size_t idx;
            int rc = search_b1_find(vals, lo16, dir, N, vals[i], &idx);
            CHECK(rc == INT32_SEARCH_OK, "hit should return OK");
            CHECK(vals[idx] == vals[i], "hit should find correct value");
        }
    });

    TEST("Int32 normal: misses return NOT_FOUND", {
        size_t idx;
        int rc = search_b1_find(vals, lo16, dir, N, INT32_MAX, &idx);
        CHECK(rc == INT32_SEARCH_ERR_NOT_FOUND, "miss should return NOT_FOUND");
    });

    free(dir);
    free(lo16);
    free(vals);
}

static void run_sanity_int64(void)
{
    printf("\n=== Sanity: Int64 normal dir ===\n");

    const size_t N = 5000;
    int64_t *vals = (int64_t *)malloc(N * sizeof(int64_t));
    for (size_t i = 0; i < N; i++)
        vals[i] = (int64_t)((i * 7919 + 1) % 2000000000 - 1000000000);
    qsort(vals, N, sizeof(int64_t), cmp_int64);

    int32_t *dir = build_dir_int64_fuzz(vals, N);

    TEST("Int64 normal: all hits correct", {
        for (size_t i = 0; i < N; i += 500) {
            size_t rc = search_int64_b1(vals, dir, N, vals[i]);
            CHECK(rc != (size_t)-1, "hit should not return -1");
            CHECK(vals[rc] == vals[i], "hit should find correct value");
        }
    });

    TEST("Int64 normal: misses return -1", {
        size_t rc = search_int64_b1(vals, dir, N, INT64_MAX);
        CHECK(rc == (size_t)-1, "miss should return -1");
    });

    free(dir);
    free(vals);
}

/* ========================================================================
 * main
 * ======================================================================== */

int main(void)
{
    printf("=== B1 Dir Fuzz (ACT-62) ===\n");
    printf("SG-3: 注入 start/end 异常值对, 零崩溃验证\n\n");

    run_sanity_int32();
    run_sanity_int64();
    run_int32_fuzz();
    run_int64_fuzz();

    printf("\n=== Result: %d failures ===\n", g_failures);
    return g_failures > 0 ? 1 : 0;
}
