/* T1 sanity test: 验证字段原子化后 Phase 1 行为不变 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../include/int64_search.h"

int main(void) {
    const size_t N = 10000;
    int64_t *data = (int64_t *)malloc(N * sizeof(int64_t));
    if (!data) return 1;

    /* 生成 0..N-1 的有序数据 */
    for (size_t i = 0; i < N; i++) data[i] = (int64_t)i;

    /* create */
    int64_search_t h = int64_search_create(data, N, NULL);
    if (!h) { fprintf(stderr, "create failed\n"); return 1; }

    /* find existing */
    size_t idx;
    int rc = int64_search_find(h, 5000, &idx);
    if (rc != INT64_SEARCH_OK || idx != 5000) {
        fprintf(stderr, "find 5000 failed: rc=%d idx=%zu\n", rc, idx);
        return 1;
    }

    /* find missing */
    rc = int64_search_find(h, 50000, &idx);
    if (rc != INT64_SEARCH_ERR_NOT_FOUND) {
        fprintf(stderr, "find 50000 expected NOT_FOUND, got rc=%d\n", rc);
        return 1;
    }

    /* rebuild */
    for (size_t i = 0; i < N; i++) data[i] = (int64_t)(i * 2);
    rc = int64_search_rebuild(h, data, N);
    if (rc != INT64_SEARCH_OK) {
        fprintf(stderr, "rebuild failed: rc=%d\n", rc);
        return 1;
    }

    /* find after rebuild */
    rc = int64_search_find(h, 10000, &idx);
    if (rc != INT64_SEARCH_OK || idx != 5000) {
        fprintf(stderr, "find 10000 (after rebuild) failed: rc=%d idx=%zu\n", rc, idx);
        return 1;
    }

    /* destroy */
    int64_search_destroy(h);
    free(data);

    printf("T1 sanity test PASSED\n");
    return 0;
}
