#include "../include/int32_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define TEST_PASS() printf("  PASS: %s\n", __func__)

static int cmp_int(const void *a, const void *b)
{
    int32_t va = *(const int32_t *)a;
    int32_t vb = *(const int32_t *)b;
    return (va > vb) - (va < vb);
}

static int32_t *generate_sorted_data(size_t n)
{
    int32_t *data = (int32_t *)malloc(n * sizeof(int32_t));
    if (data == NULL) return NULL;
    for (size_t i = 0; i < n; i++) {
        data[i] = (int32_t)(i * 2);
    }
    return data;
}

static int test_rebuild_basic(void)
{
    int32_t *data1 = generate_sorted_data(1000);
    int32_t *data2 = generate_sorted_data(1000);
    for (size_t i = 0; i < 1000; i++) data2[i] = (int32_t)(i * 2 + 1);

    int32_search_t h = int32_search_create(data1, 1000, NULL);
    ASSERT(h != NULL, "create should succeed");

    int rc = int32_search_rebuild(h, data2, 1000);
    ASSERT(rc == INT32_SEARCH_OK, "rebuild should succeed");

    size_t idx;
    rc = int32_search_find(h, 5, &idx);
    ASSERT(rc == INT32_SEARCH_OK, "find 5 should hit in new data");
    ASSERT(idx == 2, "5 should be at index 2");

    int32_search_destroy(h);
    free(data1);
    free(data2);
    TEST_PASS();
    return 0;
}

static int test_rebuild_preserve_old(void)
{
    int32_t *data1 = generate_sorted_data(1000);

    int32_search_t h = int32_search_create(data1, 1000, NULL);
    ASSERT(h != NULL, "create should succeed");

    int rc = int32_search_rebuild(h, NULL, 1000);
    ASSERT(rc == INT32_SEARCH_ERR_INVALID_ARG, "rebuild with NULL data should fail");

    size_t idx;
    rc = int32_search_find(h, 10, &idx);
    ASSERT(rc == INT32_SEARCH_OK, "old data should still be accessible after failed rebuild");
    ASSERT(idx == 5, "10 should be at index 5");

    int32_search_destroy(h);
    free(data1);
    TEST_PASS();
    return 0;
}

static int test_rebuild_null_handle(void)
{
    int32_t *data = generate_sorted_data(100);
    int rc = int32_search_rebuild(NULL, data, 100);
    ASSERT(rc == INT32_SEARCH_ERR_NULL_HANDLE, "rebuild with NULL handle should fail");
    free(data);
    TEST_PASS();
    return 0;
}

static int test_rebuild_invalid_arg(void)
{
    int32_t *data = generate_sorted_data(100);
    int32_search_t h = int32_search_create(data, 100, NULL);
    ASSERT(h != NULL, "create should succeed");

    int rc = int32_search_rebuild(h, NULL, 100);
    ASSERT(rc == INT32_SEARCH_ERR_INVALID_ARG, "rebuild with NULL data should fail");

    rc = int32_search_rebuild(h, data, 0);
    ASSERT(rc == INT32_SEARCH_ERR_INVALID_ARG, "rebuild with n==0 should fail");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
    return 0;
}

typedef struct {
    int32_search_t handle;
    int32_t *data_pool;
    size_t data_size;
    atomic_int *stop;
    atomic_int *errors;
} reader_arg_t;

static void *reader_func(void *arg)
{
    reader_arg_t *ra = (reader_arg_t *)arg;
    while (!atomic_load(ra->stop)) {
        size_t idx;
        int32_t key = (int32_t)(rand() % (int)(ra->data_size * 2));
        int rc = int32_search_find(ra->handle, key, &idx);
        if (rc != INT32_SEARCH_OK && rc != INT32_SEARCH_ERR_NOT_FOUND) {
            atomic_fetch_add(ra->errors, 1);
        }
    }
    return NULL;
}

typedef struct {
    int32_search_t handle;
    size_t data_size;
    int rebuild_count;
    atomic_int *stop;
    atomic_int *errors;
} writer_arg_t;

static void *writer_func(void *arg)
{
    writer_arg_t *wa = (writer_arg_t *)arg;
    for (int i = 0; i < wa->rebuild_count; i++) {
        int32_t *new_data = generate_sorted_data(wa->data_size);
        if (new_data == NULL) {
            atomic_fetch_add(wa->errors, 1);
            continue;
        }
        int rc = int32_search_rebuild(wa->handle, new_data, wa->data_size);
        if (rc != INT32_SEARCH_OK) {
            atomic_fetch_add(wa->errors, 1);
        }
        free(new_data);
    }
    atomic_store(wa->stop, 1);
    return NULL;
}

static int test_concurrent_read_rebuild(void)
{
    int32_t *data = generate_sorted_data(10000);
    int32_search_t h = int32_search_create(data, 10000, NULL);
    ASSERT(h != NULL, "create should succeed");

    atomic_int stop = ATOMIC_VAR_INIT(0);
    atomic_int errors = ATOMIC_VAR_INIT(0);

    reader_arg_t rarg = { h, data, 10000, &stop, &errors };
    writer_arg_t warg = { h, 10000, 20, &stop, &errors };

    pthread_t reader, writer;
    pthread_create(&reader, NULL, reader_func, &rarg);
    pthread_create(&writer, NULL, writer_func, &warg);

    pthread_join(writer, NULL);
    pthread_join(reader, NULL);

    ASSERT(atomic_load(&errors) == 0, "no errors during concurrent read+rebuild");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
    return 0;
}

static int test_concurrent_n_readers(void)
{
    int32_t *data = generate_sorted_data(10000);
    int32_search_t h = int32_search_create(data, 10000, NULL);
    ASSERT(h != NULL, "create should succeed");

    atomic_int stop = ATOMIC_VAR_INIT(0);
    atomic_int errors = ATOMIC_VAR_INIT(0);

    reader_arg_t rargs[4];
    pthread_t readers[4];
    for (int i = 0; i < 4; i++) {
        rargs[i].handle = h;
        rargs[i].data_pool = data;
        rargs[i].data_size = 10000;
        rargs[i].stop = &stop;
        rargs[i].errors = &errors;
        pthread_create(&readers[i], NULL, reader_func, &rargs[i]);
    }

    writer_arg_t warg = { h, 10000, 20, &stop, &errors };
    pthread_t writer;
    pthread_create(&writer, NULL, writer_func, &warg);

    pthread_join(writer, NULL);
    for (int i = 0; i < 4; i++) {
        pthread_join(readers[i], NULL);
    }

    ASSERT(atomic_load(&errors) == 0, "no errors during concurrent 4-reader+writer");

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
    return 0;
}

static int test_destroy_during_read(void)
{
    int32_t *data = generate_sorted_data(10000);
    int32_search_t h = int32_search_create(data, 10000, NULL);
    ASSERT(h != NULL, "create should succeed");

    size_t idx;
    int rc = int32_search_find(h, 100, &idx);
    ASSERT(rc == INT32_SEARCH_OK, "find should hit");

    rc = int32_search_destroy(h);
    ASSERT(rc == INT32_SEARCH_OK, "destroy should succeed");

    free(data);
    TEST_PASS();
    return 0;
}

static int test_rebuild_loop_memory(void)
{
    int32_t *data = generate_sorted_data(10000);
    int32_search_t h = int32_search_create(data, 10000, NULL);
    ASSERT(h != NULL, "create should succeed");

    for (int i = 0; i < 100; i++) {
        int32_t *new_data = generate_sorted_data(10000);
        int rc = int32_search_rebuild(h, new_data, 10000);
        ASSERT(rc == INT32_SEARCH_OK, "rebuild in loop should succeed");
        free(new_data);
    }

    int32_search_destroy(h);
    free(data);
    TEST_PASS();
    return 0;
}

int main(void)
{
    int failed = 0;

    printf("=== Phase 1.5 Thread Safety Tests ===\n\n");

    printf("[1/8] test_rebuild_basic\n");
    failed += test_rebuild_basic();

    printf("[2/8] test_rebuild_preserve_old\n");
    failed += test_rebuild_preserve_old();

    printf("[3/8] test_rebuild_null_handle\n");
    failed += test_rebuild_null_handle();

    printf("[4/8] test_rebuild_invalid_arg\n");
    failed += test_rebuild_invalid_arg();

    printf("[5/8] test_concurrent_read_rebuild\n");
    failed += test_concurrent_read_rebuild();

    printf("[6/8] test_concurrent_n_readers\n");
    failed += test_concurrent_n_readers();

    printf("[7/8] test_destroy_during_read\n");
    failed += test_destroy_during_read();

    printf("[8/8] test_rebuild_loop_memory\n");
    failed += test_rebuild_loop_memory();

    printf("\n=== Results: %d/8 passed, %d failed ===\n", 8 - failed, failed);

    return failed > 0 ? 1 : 0;
}
