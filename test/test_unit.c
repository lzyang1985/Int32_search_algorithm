#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "int32_search.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %-35s ... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_create_null_input(void)
{
    TEST("create(NULL, 0, NULL)");
    int32_search_t h = int32_search_create(NULL, 0, NULL);
    if (h == NULL) PASS(); else FAIL("expected NULL");
}

static void test_create_normal(void)
{
    TEST("create normal data");
    int32_t data[] = { 3, 1, 4, 1, 5, 9, 2, 6 };
    int32_search_t h = int32_search_create(data, 8, NULL);
    if (h != NULL) {
        int32_search_destroy(h);
        PASS();
    } else {
        FAIL("handle is NULL");
    }
}

static void test_find_null_handle(void)
{
    TEST("find(NULL, key, &idx)");
    size_t idx;
    int ret = int32_search_find(NULL, 42, &idx);
    if (ret == INT32_SEARCH_ERR_NULL_HANDLE) PASS();
    else FAIL("expected ERR_NULL_HANDLE");
}

static void test_find_null_out_index(void)
{
    TEST("find(h, key, NULL)");
    int32_t data[] = { 1, 2, 3 };
    int32_search_t h = int32_search_create(data, 3, NULL);
    if (h == NULL) { FAIL("create failed"); return; }
    int ret = int32_search_find(h, 2, NULL);
    if (ret == INT32_SEARCH_ERR_INVALID_ARG) PASS(); else FAIL("expected ERR_INVALID_ARG");
    int32_search_destroy(h);
}

static void test_find_hit(void)
{
    TEST("find single-element hit");
    int32_t data[] = { 42 };
    int32_search_t h = int32_search_create(data, 1, NULL);
    if (h == NULL) { FAIL("create failed"); return; }
    size_t idx;
    int ret = int32_search_find(h, 42, &idx);
    if (ret == INT32_SEARCH_OK && idx == 0) PASS();
    else FAIL(ret != 0 ? "not found" : "wrong index");
    int32_search_destroy(h);
}

static void test_find_miss(void)
{
    TEST("find single-element miss");
    int32_t data[] = { 42 };
    int32_search_t h = int32_search_create(data, 1, NULL);
    if (h == NULL) { FAIL("create failed"); return; }
    size_t idx;
    int ret = int32_search_find(h, 100, &idx);
    if (ret == INT32_SEARCH_ERR_NOT_FOUND) PASS();
    else FAIL("expected NOT_FOUND");
    int32_search_destroy(h);
}

static void test_destroy_null(void)
{
    TEST("destroy(NULL)");
    int ret = int32_search_destroy(NULL);
    if (ret == INT32_SEARCH_OK) PASS(); else FAIL("expected OK");
}

static void test_destroy_null_idempotent(void)
{
    TEST("destroy(NULL) idempotent");
    int32_t data[] = { 1, 2 };
    int32_search_t h = int32_search_create(data, 2, NULL);
    if (h == NULL) { FAIL("create failed"); return; }
    int32_search_destroy(h);
    int ret = int32_search_destroy(NULL);
    if (ret == INT32_SEARCH_OK) PASS(); else FAIL("destroy(NULL) failed");
}

static void test_double_destroy(void)
{
    TEST("double destroy (UB, not testable under ASan)");
    printf("SKIP (double-free is UB; ASan aborts on first error, cannot verify in-process)\n");
}

static void test_version(void)
{
    TEST("version() non-null");
    const char *v = int32_search_version();
    if (v != NULL && strlen(v) > 0) PASS(); else FAIL("empty version");
}

int main(void)
{
    printf("Unit Tests\n==========\n\n");

    test_create_null_input();
    test_create_normal();
    test_find_null_handle();
    test_find_null_out_index();
    test_find_hit();
    test_find_miss();
    test_destroy_null();
    test_destroy_null_idempotent();
    test_double_destroy();
    test_version();

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
