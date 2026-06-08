/* T2+T3 sanity test: 多线程 find 并发 + destroy 等待
 * 不依赖 TSan,仅验证: 多 reader 并发 + destroy 等所有 reader 退出后释放
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include "../include/int64_search.h"

#define N         100000
#define N_THREADS 4
#define ITER      50000

static int64_search_t g_handle;
static int            g_find_ok = 0;
static int            g_find_miss = 0;

static void *reader_main(void *arg) {
    intptr_t tid = (intptr_t)arg;
    int64_t key = (int64_t)((tid * 12345 + 7) % N);
    size_t idx;
    int local_ok = 0, local_miss = 0;
    for (int i = 0; i < ITER; i++) {
        int rc = int64_search_find(g_handle, key, &idx);
        if (rc == INT64_SEARCH_OK) local_ok++;
        else if (rc == INT64_SEARCH_ERR_NOT_FOUND) local_miss++;
        /* key 永远在 [0, N) 内,理论上全部 OK */
    }
    /* 通过原子操作累加(Phase 2 后 impl 是 atomic) */
    __sync_fetch_and_add(&g_find_ok,   local_ok);
    __sync_fetch_and_add(&g_find_miss, local_miss);
    return NULL;
}

int main(void) {
    int64_t *data = (int64_t *)malloc(N * sizeof(int64_t));
    for (size_t i = 0; i < N; i++) data[i] = (int64_t)i;

    g_handle = int64_search_create(data, N, NULL);
    if (!g_handle) { fprintf(stderr, "create failed\n"); return 1; }

    /* 启动 N_THREADS reader */
    pthread_t threads[N_THREADS];
    for (intptr_t t = 0; t < N_THREADS; t++) {
        if (pthread_create(&threads[t], NULL, reader_main, (void *)t) != 0) {
            fprintf(stderr, "pthread_create failed\n"); return 1;
        }
    }

    /* 等待所有 reader 完成 */
    for (int t = 0; t < N_THREADS; t++) pthread_join(threads[t], NULL);

    /* destroy(应立即返回,因为 reader_count=0) */
    int rc = int64_search_destroy(g_handle);
    if (rc != INT64_SEARCH_OK) { fprintf(stderr, "destroy failed: %d\n", rc); return 1; }

    free(data);

    int expected_total = N_THREADS * ITER;
    if (g_find_ok != expected_total) {
        fprintf(stderr, "T2+T3 test FAILED: ok=%d, expected=%d, miss=%d\n",
                g_find_ok, expected_total, g_find_miss);
        return 1;
    }

    printf("T2+T3 multi-thread test PASSED: %d find(OK) across %d threads\n",
           g_find_ok, N_THREADS);
    return 0;
}
