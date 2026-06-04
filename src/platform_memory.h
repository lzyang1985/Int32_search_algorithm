#ifndef INT32_SEARCH_PLATFORM_MEMORY_H
#define INT32_SEARCH_PLATFORM_MEMORY_H

#include <stddef.h>

void *platform_aligned_alloc(size_t size);
void platform_aligned_free(void *ptr);

#endif
