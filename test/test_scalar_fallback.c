#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "int32_search.h"

#define TEST(name) printf("  TEST: %s ... ", name)
#define PASS()     printf("PASS\n")
#define FAIL(msg)  printf("FAIL: %s\n", msg)

int main(void)
{
    printf("=== Scalar Fallback Coverage Test (AVX2 machine) ===\n\n");

    {
        int32_t data[] = { 1, 3, 5, 7, 9, 11, 13, 15, 17, 19 };
        size_t n = 10;

        TEST("create n=10 (force scalar path, n << 10M threshold)");
        int32_search_t h = int32_search_create(data, n, NULL);
        if (h == NULL) { FAIL("create returned NULL"); return 1; }
        PASS();

        TEST("find existing key=7");
        size_t idx;
        int ret = int32_search_find(h, 7, &idx);
        if (ret != 0) { FAIL("unexpected NOT_FOUND"); int32_search_destroy(h); return 1; }
        if (idx != 3) { FAIL("wrong index"); int32_search_destroy(h); return 1; }
        PASS();

        TEST("find missing key=8");
        ret = int32_search_find(h, 8, &idx);
        if (ret == 0) { FAIL("should be NOT_FOUND"); int32_search_destroy(h); return 1; }
        PASS();

        TEST("find boundary key=1 (first)");
        ret = int32_search_find(h, 1, &idx);
        if (ret != 0 || idx != 0) { FAIL("wrong result for boundary key=1"); int32_search_destroy(h); return 1; }
        PASS();

        TEST("find boundary key=19 (last)");
        ret = int32_search_find(h, 19, &idx);
        if (ret != 0 || idx != 9) { FAIL("wrong result for boundary key=19"); int32_search_destroy(h); return 1; }
        PASS();

        int32_search_destroy(h);
    }

    {
        TEST("create n=10000 (still scalar path, n < 10M)");
        int32_t *data = (int32_t *)malloc(10000 * sizeof(int32_t));
        if (data == NULL) { FAIL("malloc"); return 1; }
        for (size_t i = 0; i < 10000; i++) data[i] = (int32_t)(i * 2);

        int32_search_t h = int32_search_create(data, 10000, NULL);
        if (h == NULL) { FAIL("create returned NULL"); free(data); return 1; }
        PASS();

        TEST("batch 1000 queries (scalar path on AVX2 machine)");
        int mismatches = 0;
        for (size_t i = 0; i < 1000; i++) {
            int32_t key = (int32_t)((i * 7 + 3) % 20000);
            size_t idx;
            int ret = int32_search_find(h, key, &idx);
            int found = 0;
            for (size_t j = 0; j < 10000; j++) {
                if (data[j] == key) { found = 1; break; }
            }
            if ((ret == 0) != found) mismatches++;
            else if (ret == 0 && data[idx] != key) mismatches++;
        }
        if (mismatches > 0) { FAIL("mismatches found"); free(data); int32_search_destroy(h); return 1; }
        PASS();

        int32_search_destroy(h);
        free(data);
    }

    printf("\n=== All scalar fallback tests PASSED ===\n");
    printf("Verified: API else branch (scalar fallback) works correctly on AVX2 machine\n");
    return 0;
}
