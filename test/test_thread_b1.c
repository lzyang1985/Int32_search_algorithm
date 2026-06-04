#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "int32_search.h"

#ifdef _WIN32
#include <windows.h>
#define THREAD_FUNC DWORD WINAPI
#define THREAD_RET DWORD
#define THREAD_CREATE(h, fn, arg) (*(h) = CreateThread(NULL, 0, fn, arg, 0, NULL), *(h) != NULL)
#define THREAD_JOIN(h) WaitForSingleObject(h, INFINITE)
typedef HANDLE thread_t;
#else
#include <pthread.h>
#include <unistd.h>
#define THREAD_FUNC void*
#define THREAD_RET void*
#define THREAD_CREATE(h, fn, arg) (pthread_create(h, NULL, fn, arg) == 0)
#define THREAD_JOIN(h) pthread_join(h, NULL)
typedef pthread_t thread_t;
#endif

#define ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

#define TEST_PASS() printf("  PASS\n"); return 0

static int32_t rand32(void)
{
    return (int32_t)(((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 2) ^ (uint32_t)rand());
}

static int cmp_int32(const void *a, const void *b)
{
    int32_t ia = *(const int32_t *)a;
    int32_t ib = *(const int32_t *)b;
    return (ia > ib) - (ia < ib);
}

static int32_t *gen_sorted_data(int n)
{
    int32_t *data = (int32_t *)malloc((size_t)n * sizeof(int32_t));
    for (int i = 0; i < n; i++) data[i] = rand32();
    qsort(data, (size_t)n, sizeof(int32_t), cmp_int32);
    return data;
}

static int test_b1_rebuild_basic(void)
{
    printf("test_b1_rebuild_basic...");
    int n = 5000;
    int32_t *data1 = gen_sorted_data(n);
    int32_t *data2 = gen_sorted_data(n);

    int32_search_t h = int32_search_create(data1, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    int rc = int32_search_rebuild(h, data2, (size_t)n);
    ASSERT(rc == INT32_SEARCH_OK, "rebuild failed");

    size_t idx;
    rc = int32_search_find(h, data2[n / 2], &idx);
    ASSERT(rc == INT32_SEARCH_OK, "find after rebuild failed");
    ASSERT(data2[idx] == data2[n / 2], "value mismatch after rebuild");

    int32_search_destroy(h);
    free(data1);
    free(data2);
    TEST_PASS();
}

static int test_b1_rebuild_preserve_old(void)
{
    printf("test_b1_rebuild_preserve_old...");
    int n = 1000;
    int32_t *data = gen_sorted_data(n);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    int rc = int32_search_rebuild(h, NULL, (size_t)n);
    ASSERT(rc == INT32_SEARCH_ERR_INVALID_ARG, "rebuild null data should fail");

    size_t idx;
    rc = int32_search_find(h, data[n / 2], &idx);
    ASSERT(rc == INT32_SEARCH_OK, "old data should still be usable");

    rc = int32_search_rebuild(h, (const int32_t *)&(int32_t){1}, 0);
    ASSERT(rc == INT32_SEARCH_ERR_INVALID_ARG, "rebuild n=0 should fail");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

typedef struct {
    int32_search_t handle;
    atomic_int *stop_flag;
    atomic_int *error_count;
    int data_size;
} reader_arg_t;

static THREAD_FUNC reader_thread(void *arg)
{
    reader_arg_t *ra = (reader_arg_t *)arg;
    while (!atomic_load(ra->stop_flag)) {
        size_t idx;
        int32_t key = (int32_t)rand();
        int rc = int32_search_find(ra->handle, key, &idx);
        if (rc != INT32_SEARCH_OK && rc != INT32_SEARCH_ERR_NOT_FOUND) {
            atomic_fetch_add(ra->error_count, 1);
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

typedef struct {
    int32_search_t handle;
    atomic_int *error_count;
    int data_size;
    int iterations;
} writer_arg_t;

static THREAD_FUNC writer_thread(void *arg)
{
    writer_arg_t *wa = (writer_arg_t *)arg;
    for (int i = 0; i < wa->iterations; i++) {
        int32_t *new_data = gen_sorted_data(wa->data_size);
        int rc = int32_search_rebuild(wa->handle, new_data, (size_t)wa->data_size);
        if (rc != INT32_SEARCH_OK && rc != INT32_SEARCH_ERR_MEMORY) {
            atomic_fetch_add(wa->error_count, 1);
        }
        free(new_data);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int test_b1_concurrent_read_rebuild(void)
{
    printf("test_b1_concurrent_read_rebuild...");
    int n = 5000;
    int32_t *data = gen_sorted_data(n);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    atomic_int stop_flag = 0;
    atomic_int error_count = 0;

    reader_arg_t ra = { h, &stop_flag, &error_count, n };
    writer_arg_t wa = { h, &error_count, n, 10 };

    thread_t r_thread, w_thread;
    ASSERT(THREAD_CREATE(&r_thread, reader_thread, &ra), "reader thread create");
    ASSERT(THREAD_CREATE(&w_thread, writer_thread, &wa), "writer thread create");

    THREAD_JOIN(w_thread);
    atomic_store(&stop_flag, 1);
    THREAD_JOIN(r_thread);

    ASSERT(atomic_load(&error_count) == 0, "concurrent errors");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_concurrent_n_readers(void)
{
    printf("test_b1_concurrent_n_readers...");
    int n = 5000;
    int n_readers = 4;
    int32_t *data = gen_sorted_data(n);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    atomic_int stop_flag = 0;
    atomic_int error_count = 0;

    reader_arg_t ra[4];
    thread_t r_threads[4];
    for (int i = 0; i < n_readers; i++) {
        ra[i].handle = h;
        ra[i].stop_flag = &stop_flag;
        ra[i].error_count = &error_count;
        ra[i].data_size = n;
        ASSERT(THREAD_CREATE(&r_threads[i], reader_thread, &ra[i]), "reader create");
    }

    writer_arg_t wa = { h, &error_count, n, 10 };
    thread_t w_thread;
    ASSERT(THREAD_CREATE(&w_thread, writer_thread, &wa), "writer create");

    THREAD_JOIN(w_thread);
    atomic_store(&stop_flag, 1);
    for (int i = 0; i < n_readers; i++) {
        THREAD_JOIN(r_threads[i]);
    }

    ASSERT(atomic_load(&error_count) == 0, "concurrent errors");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_path_switch(void)
{
    printf("test_b1_path_switch...");
    int n = 5000;
    int32_t *data1 = gen_sorted_data(n);

    int32_search_t h = int32_search_create(data1, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    int n2 = 1000;
    uint16_t same_high = 0x5555;
    int32_t *data2 = (int32_t *)malloc((size_t)n2 * sizeof(int32_t));
    for (int i = 0; i < n2; i++) data2[i] = ((int32_t)same_high << 16) | (int32_t)i;
    qsort(data2, (size_t)n2, sizeof(int32_t), cmp_int32);

    int rc = int32_search_rebuild(h, data2, (size_t)n2);
    ASSERT(rc == INT32_SEARCH_OK, "rebuild to A failed");

    size_t idx;
    rc = int32_search_find(h, data2[n2 / 2], &idx);
    ASSERT(rc == INT32_SEARCH_OK, "find after path switch B1→A failed");
    ASSERT(data2[idx] == data2[n2 / 2], "value mismatch after path switch");

    int32_search_destroy(h);
    free(data1);
    free(data2);
    TEST_PASS();
}

static int test_b1_rebuild_loop_memory(void)
{
    printf("test_b1_rebuild_loop_memory...");
    int n = 2000;
    int32_t *data = gen_sorted_data(n);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    for (int i = 0; i < 100; i++) {
        int32_t *new_data = gen_sorted_data(n);
        int rc = int32_search_rebuild(h, new_data, (size_t)n);
        ASSERT(rc == INT32_SEARCH_OK, "rebuild loop failed");
        free(new_data);
    }

    size_t idx;
    int rc = int32_search_find(h, data[n / 2], &idx);
    ASSERT(rc == INT32_SEARCH_OK || rc == INT32_SEARCH_ERR_NOT_FOUND, "find after 100 rebuilds");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
}

static int test_b1_destroy_during_read(void)
{
    printf("test_b1_destroy_during_read...");
    int n = 1000;
    int32_t *data = gen_sorted_data(n);

    int32_search_t h = int32_search_create(data, (size_t)n, NULL);
    ASSERT(h != NULL, "create failed");

    atomic_int error_count = 0;
    atomic_int stop_flag = 0;
    reader_arg_t ra = { h, &stop_flag, &error_count, n };
    thread_t r_thread;
    ASSERT(THREAD_CREATE(&r_thread, reader_thread, &ra), "reader create");

#ifdef _WIN32
    Sleep(500);
#else
    usleep(500000);
#endif

    atomic_store(&stop_flag, 1);
#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);
#endif

    int32_search_destroy(h);
    THREAD_JOIN(r_thread);

    ASSERT(atomic_load(&error_count) == 0, "destroy during read errors");
    free(data);
    TEST_PASS();
}

int main(void)
{
    int failures = 0;
    failures += test_b1_rebuild_basic();
    failures += test_b1_rebuild_preserve_old();
    failures += test_b1_concurrent_read_rebuild();
    failures += test_b1_concurrent_n_readers();
    failures += test_b1_path_switch();
    failures += test_b1_rebuild_loop_memory();
    failures += test_b1_destroy_during_read();
    printf("\n=== B1 Thread: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
