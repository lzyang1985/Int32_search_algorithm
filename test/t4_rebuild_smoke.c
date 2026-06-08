/* T4 sanity test: rebuild COW 关键场景验证
 * - rebuild 1000 次后查询正确
 * - rebuild 后旧 key NOT_FOUND,新 key OK
 * - 多次 rebuild 不崩溃(内存正确释放)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../include/int64_search.h"

#define N 10000

int main(void) {
    int64_t *data = (int64_t *)malloc(N * sizeof(int64_t));
    if (!data) return 1;

    /* 初始数据: 0..N-1 */
    for (size_t i = 0; i < N; i++) data[i] = (int64_t)i;

    int64_search_t h = int64_search_create(data, N, NULL);
    if (!h) { fprintf(stderr, "create failed\n"); return 1; }

    /* 验证初值 */
    size_t idx;
    int rc = int64_search_find(h, 5000, &idx);
    if (rc != INT64_SEARCH_OK || idx != 5000) {
        fprintf(stderr, "T4-L1: initial find failed\n"); return 1;
    }

    /* === T4-L1: rebuild 后旧 key NOT_FOUND,新 key OK === */
    for (size_t i = 0; i < N; i++) data[i] = (int64_t)(i * 3);  /* 0, 3, 6, ... */
    rc = int64_search_rebuild(h, data, N);
    if (rc != INT64_SEARCH_OK) { fprintf(stderr, "T4-L1: rebuild failed\n"); return 1; }

    /* 旧 key=5000(0..N-1) 应 NOT_FOUND */
    rc = int64_search_find(h, 5000, &idx);
    if (rc != INT64_SEARCH_ERR_NOT_FOUND) {
        fprintf(stderr, "T4-L1: 5000 should be NOT_FOUND, got rc=%d\n", rc); return 1;
    }
    /* 新 key=15000(0*3..N*3-1),15000=5000*3 应在 idx=5000 */
    rc = int64_search_find(h, 15000, &idx);
    if (rc != INT64_SEARCH_OK || idx != 5000) {
        fprintf(stderr, "T4-L1: 15000 should be idx=5000, got rc=%d idx=%zu\n", rc, idx); return 1;
    }
    printf("T4-L1: rebuild COW correct\n");

    /* === T4-L2: rebuild 1000 次稳定性 + 内存正确释放 === */
    for (int round = 0; round < 1000; round++) {
        int64_t base = (int64_t)round;
        for (size_t i = 0; i < N; i++) data[i] = base + (int64_t)i;
        rc = int64_search_rebuild(h, data, N);
        if (rc != INT64_SEARCH_OK) {
            fprintf(stderr, "T4-L2: rebuild round %d failed: rc=%d\n", round, rc);
            return 1;
        }
        /* 抽样验证: round=base 时 key=base+5000 必在 idx=5000 */
        rc = int64_search_find(h, base + 5000, &idx);
        if (rc != INT64_SEARCH_OK || idx != 5000) {
            fprintf(stderr, "T4-L2: round %d find failed\n", round); return 1;
        }
    }
    printf("T4-L2: 1000 rebuild rounds stable\n");

    /* === T4-L3: bloom_bypass 状态保持(Q-A2 决议) === */
    rc = int64_search_set_bloom_bypass(h, 1);
    if (rc != INT64_SEARCH_OK) { fprintf(stderr, "T4-L3: set bypass failed\n"); return 1; }
    int bypass = int64_search_get_bloom_bypass(h);
    if (bypass != 1) { fprintf(stderr, "T4-L3: get bypass expected 1, got %d\n", bypass); return 1; }

    /* rebuild 后 bloom_bypass 应保持 */
    for (size_t i = 0; i < N; i++) data[i] = (int64_t)(i * 2);
    rc = int64_search_rebuild(h, data, N);
    if (rc != INT64_SEARCH_OK) { fprintf(stderr, "T4-L3: rebuild failed\n"); return 1; }
    bypass = int64_search_get_bloom_bypass(h);
    if (bypass != 1) { fprintf(stderr, "T4-L3: bypass should remain 1, got %d\n", bypass); return 1; }
    printf("T4-L3: bloom_bypass state preserved after rebuild\n");

    /* === T4-L4: version 升级验证 === */
    const char *v = int64_search_version();
    if (strcmp(v, "libint64search 0.2.0") != 0) {
        fprintf(stderr, "T4-L4: version expected '0.2.0', got '%s'\n", v); return 1;
    }
    printf("T4-L4: version = '%s' (Phase 2)\n", v);

    int64_search_destroy(h);
    free(data);

    printf("T4 sanity test PASSED\n");
    return 0;
}
