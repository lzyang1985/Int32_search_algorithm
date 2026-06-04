#include <stdint.h>
#include <stddef.h>

size_t search_int64_scalar(const int64_t *vals, size_t n, int64_t target) {
    if (n == 0 || vals == NULL) return (size_t)-1;

    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo < n && vals[lo] == target)
        return lo;
    return (size_t)-1;
}
