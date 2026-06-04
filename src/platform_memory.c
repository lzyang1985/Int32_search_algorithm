#include "platform_memory.h"
#include <mm_malloc.h>

void *platform_aligned_alloc(size_t size)
{
    return _mm_malloc(size, 32);
}

void platform_aligned_free(void *ptr)
{
    if (ptr != NULL) _mm_free(ptr);
}
