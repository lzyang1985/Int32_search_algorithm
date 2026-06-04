#include "search_scalar.h"
#include "internal.h"

int32_t search_scalar_find(const int32_t *vals, size_t n,
                           int32_t target, size_t *out_index)
{
    if (n == 0) return INT32_SEARCH_ERR_NOT_FOUND;
    if (vals == NULL) return INT32_SEARCH_ERR_INVALID_ARG;

    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo < n && vals[lo] == target) {
        if (out_index != NULL) *out_index = lo;
        return INT32_SEARCH_OK;
    }

    return INT32_SEARCH_ERR_NOT_FOUND;
}

size_t search_scalar_lower_bound(const int32_t *vals, size_t n, int32_t target)
{
    if (n == 0) return 0;

    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }

    return lo;
}

size_t search_scalar_upper_bound(const int32_t *vals, size_t n, int32_t target)
{
    if (n == 0) return 0;

    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] <= target)
            lo = mid + 1;
        else
            hi = mid;
    }

    return lo;
}
