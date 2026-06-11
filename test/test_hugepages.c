/* ============================================================================
 * test_hugepages.c — Huge Pages 快速验证 (ACT-65)
 *
 * 对比: 默认分配 vs madvise(MADV_HUGEPAGE) 的 TLB 行为差异
 *
 * 由于服务器 perf 不可用 (内核版本不匹配), 采用:
 *   1. clock_gettime 计时对比
 *   2. /proc/self/smaps 读取 AnonHugePages
 *
 * 编译 (Linux):
 *   gcc -O3 -std=c11 -Wall -Wextra -mavx2 -Iinclude -Isrc \
 *       test/test_hugepages.c libint64search.a -o hugepages_test -lm -lrt
 *
 * 验收标准 (ACT-65):
 *   产出 TLB 行为对比报告
 * ========================================================================== */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#include "int64_search.h"

/* ── 计时 ── */
static double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ── 从 /proc/self/smaps 统计 AnonHugePages ── */
static long get_anon_hugepages_kb(void)
{
    long total = 0;
    FILE *fp = fopen("/proc/self/smaps", "r");
    if (!fp) return -1;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "AnonHugePages:", 14) == 0) {
            long kb = 0;
            sscanf(line + 14, "%ld", &kb);
            total += kb;
        }
    }
    fclose(fp);
    return total;
}

/* ── 生成测试数据 ── */
static int cmp_int64(const void *a, const void *b)
{
    int64_t ia = *(const int64_t *)a;
    int64_t ib = *(const int64_t *)b;
    return (ia > ib) - (ia < ib);
}

static int64_t *generate_data(size_t n)
{
    int64_t *data = (int64_t *)malloc(n * sizeof(int64_t));
    if (!data) return NULL;
    for (size_t i = 0; i < n; i++)
        data[i] = (int64_t)((i * 7919 + 1) % 2000000000 - 1000000000);
    qsort(data, n, sizeof(int64_t), cmp_int64);
    return data;
}

/* ── 尝试对分配的内存启用 Transparent Huge Pages ── */
static void try_madvise_hugepage(void *ptr, size_t size)
{
    if (!ptr) return;
    int rc = madvise(ptr, size, MADV_HUGEPAGE);
    if (rc != 0) {
        perror("  madvise(MADV_HUGEPAGE)");
    }
}

/* ── 运行单轮 benchmark ── */
static double run_bench(int64_search_t h, const int64_t *queries,
                        size_t n_queries)
{
    double t0 = get_time_sec();
    for (size_t i = 0; i < n_queries; i++) {
        size_t idx;
        volatile int rc = int64_search_find(h, queries[i], &idx);
        (void)rc;
    }
    return get_time_sec() - t0;
}

/* ======================================================================== */

int main(void)
{
    const size_t N_DATA    = 10000000;  /* 10M */
    const size_t N_QUERIES = 500000;    /* 500K */
    const int    N_ROUNDS  = 3;

    printf("=== Huge Pages 快速验证 (ACT-65) ===\n");
    printf("数据规模: %zu (%.0f MB)\n", N_DATA,
           (double)(N_DATA * sizeof(int64_t)) / (1024.0 * 1024.0));
    printf("查询数:   %zu\n", N_QUERIES);
    printf("注意: perf 不可用 (内核 5.15.0-30 工具包 5.15.0-181 不匹配)\n");
    printf("      采用 clock_gettime 计时 + smaps AnonHugePages 替代\n\n");

    /* ── 生成数据 ── */
    printf("[1] 生成 %zu int64 排序数据 ...\n", N_DATA);
    fflush(stdout);
    int64_t *data = generate_data(N_DATA);
    if (!data) { fprintf(stderr, "FAIL: generate_data\n"); return 1; }
    printf("  OK\n");

    /* ── 生成查询 ── */
    printf("[2] 生成 %zu 查询 (50%% hit) ...\n", N_QUERIES);
    fflush(stdout);
    int64_t *queries = (int64_t *)malloc(N_QUERIES * sizeof(int64_t));
    if (!queries) { fprintf(stderr, "FAIL: malloc queries\n"); free(data); return 1; }
    for (size_t i = 0; i < N_QUERIES; i++) {
        if (i % 2 == 0)
            queries[i] = data[(size_t)rand() % N_DATA];
        else
            queries[i] = (int64_t)(((int64_t)rand() << 32) | (uint32_t)rand());
    }
    printf("  OK\n\n");

    /* ═══════════════════════════════════════════════════════════════════
     * 测试 A: 默认分配 (无 madvise)
     * ═══════════════════════════════════════════════════════════════════ */
    printf("=== Test A: 默认分配 (baseline) ===\n");
    fflush(stdout);

    long hp_before_a = get_anon_hugepages_kb();
    if (hp_before_a < 0) {
        printf("  WARNING: cannot read /proc/self/smaps (permission denied?)\n");
    } else {
        printf("  AnonHugePages (分配前): %ld kB\n", hp_before_a);
    }

    int64_search_t h_a = int64_search_create(data, N_DATA, NULL);
    if (!h_a) { fprintf(stderr, "FAIL: create handle A\n"); free(queries); free(data); return 1; }
    printf("  int64_search_create OK\n");

    long hp_after_a = get_anon_hugepages_kb();
    if (hp_after_a >= 0)
        printf("  AnonHugePages (分配后): %ld kB (delta: %ld kB)\n",
               hp_after_a, hp_after_a - hp_before_a);

    /* warmup */
    run_bench(h_a, queries, N_QUERIES / 10);
    printf("  Warmup done\n");

    double total_a = 0.0;
    for (int r = 0; r < N_ROUNDS; r++) {
        double t = run_bench(h_a, queries, N_QUERIES);
        printf("  Round %d: %.3f ms (%.2f ns/query)\n",
               r + 1, t * 1000.0, t / (double)N_QUERIES * 1e9);
        total_a += t;
    }
    double avg_a = total_a / N_ROUNDS;
    printf("  Average: %.3f ms (%.2f ns/query)\n\n",
           avg_a * 1000.0, avg_a / (double)N_QUERIES * 1e9);

    int64_search_destroy(h_a);

    /* ═══════════════════════════════════════════════════════════════════
     * 测试 B: madvise(MADV_HUGEPAGE) — 对 vals 数组启用
     * ═══════════════════════════════════════════════════════════════════ */
    printf("=== Test B: madvise(MADV_HUGEPAGE) ===\n");
    fflush(stdout);

    /* 对原始 data 数组尝试 MADV_HUGEPAGE */
    try_madvise_hugepage(data, N_DATA * sizeof(int64_t));
    printf("  madvise(MADV_HUGEPAGE) on data array\n");

    long hp_before_b = get_anon_hugepages_kb();
    if (hp_before_b >= 0)
        printf("  AnonHugePages (create前): %ld kB\n", hp_before_b);

    int64_search_t h_b = int64_search_create(data, N_DATA, NULL);
    if (!h_b) { fprintf(stderr, "FAIL: create handle B\n"); free(queries); free(data); return 1; }
    printf("  int64_search_create OK\n");

    long hp_after_b = get_anon_hugepages_kb();
    if (hp_after_b >= 0)
        printf("  AnonHugePages (create后): %ld kB (delta: %ld kB)\n",
               hp_after_b, hp_after_b - hp_before_b);

    /* warmup */
    run_bench(h_b, queries, N_QUERIES / 10);
    printf("  Warmup done\n");

    double total_b = 0.0;
    for (int r = 0; r < N_ROUNDS; r++) {
        double t = run_bench(h_b, queries, N_QUERIES);
        printf("  Round %d: %.3f ms (%.2f ns/query)\n",
               r + 1, t * 1000.0, t / (double)N_QUERIES * 1e9);
        total_b += t;
    }
    double avg_b = total_b / N_ROUNDS;
    printf("  Average: %.3f ms (%.2f ns/query)\n\n",
           avg_b * 1000.0, avg_b / (double)N_QUERIES * 1e9);

    int64_search_destroy(h_b);

    /* ═══════════════════════════════════════════════════════════════════
     * 对比报告
     * ═══════════════════════════════════════════════════════════════════ */
    printf("==================================================\n");
    printf("  TLB / Huge Pages 对比报告\n");
    printf("==================================================\n");
    printf("  %-25s %15s %15s\n", "", "Baseline (A)", "HugePage (B)");
    printf("  %-25s %15s %15s\n", "-------------------------", "---------------", "---------------");

    if (hp_after_a >= 0 && hp_after_b >= 0) {
        printf("  %-25s %15ld %15ld\n",
               "AnonHugePages (kB)", hp_after_a, hp_after_b);
    }
    printf("  %-25s %14.2f %14.2f\n",
           "Avg ns/query", avg_a / (double)N_QUERIES * 1e9,
           avg_b / (double)N_QUERIES * 1e9);
    printf("  %-25s %14.3f %14.3f\n",
           "Avg ms (500K queries)", avg_a * 1000.0, avg_b * 1000.0);

    if (avg_a > 0.0) {
        double ratio = (avg_a - avg_b) / avg_a * 100.0;
        printf("\n  HugePage vs Baseline: %.2f%% %s\n",
               (ratio > 0 ? ratio : -ratio),
               (ratio > 0 ? "faster" : "slower"));
    }

    printf("\n  结论:\n");
    printf("  - perf 不可用: 内核与工具包版本不匹配\n");
    printf("  - TLB miss 无法直接测量, 以 AnonHugePages + 计时替代\n");
    printf("  - MADV_HUGEPAGE 对 int64_search 数据集的 TLB 影响如上\n");
    printf("  - 建议: 在 perf 可用的机器上补充 dTLB-load-misses 测量\n");
    printf("==================================================\n");

    free(queries);
    free(data);
    return 0;
}
