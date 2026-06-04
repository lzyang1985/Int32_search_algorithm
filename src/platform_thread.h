#ifndef INT32_SEARCH_PLATFORM_THREAD_H
#define INT32_SEARCH_PLATFORM_THREAD_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define atomic_ptr_load(ptr, order)       atomic_load_explicit(ptr, order)
#define atomic_ptr_store(ptr, val, order) atomic_store_explicit(ptr, val, order)
#define atomic_ptr_exchange(ptr, val, order) atomic_exchange_explicit(ptr, val, order)

#define atomic_size_load(ptr, order)       atomic_load_explicit(ptr, order)
#define atomic_size_store(ptr, val, order) atomic_store_explicit(ptr, val, order)
#define atomic_size_fetch_add(ptr, val, order) atomic_fetch_add_explicit(ptr, val, order)
#define atomic_size_fetch_sub(ptr, val, order) atomic_fetch_sub_explicit(ptr, val, order)

#ifdef _WIN32
  #include <windows.h>
  #if defined(__x86_64__) || defined(__i386__) || defined(_M_AMD64) || defined(_M_IX86)
    #include <immintrin.h>
    #define platform_thread_yield() do { _mm_pause(); } while(0)
  #else
    #define platform_thread_yield() Sleep(0)
  #endif
#else
  #include <sched.h>
  #define platform_thread_yield() sched_yield()
#endif

#endif
