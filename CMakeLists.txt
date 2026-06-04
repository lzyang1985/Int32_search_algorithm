cmake_minimum_required(VERSION 3.10)
project(libint32search C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall -Wextra")

include_directories(include src)

set(SRC_FILES
    src/platform_memory.c
    src/platform_cpu.c
    src/build_sorted.c
    src/build_dir.c
    src/build_b1.c
    src/build_decision.c
    src/search_scalar.c
    src/api.c)

set_source_files_properties(src/search_avx2.c PROPERTIES COMPILE_FLAGS "-mavx2")
set_source_files_properties(src/search_b1.c PROPERTIES COMPILE_FLAGS "-mavx2")
set_source_files_properties(src/bloom_filter.c PROPERTIES COMPILE_FLAGS "-DINT32_SEARCH_USE_BLOOM")
set(AVX2_FILES src/search_avx2.c src/search_b1.c)

add_library(int32search STATIC ${SRC_FILES} ${AVX2_FILES} src/bloom_filter.c)

add_executable(int32search_test
    test/test_unit.c
    test/test_correctness.c
    test/test_boundary.c)
target_compile_options(int32search_test PRIVATE -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG)
target_link_libraries(int32search_test int32search m)

add_executable(int32search_range_test test/test_range.c)
target_compile_options(int32search_range_test PRIVATE -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG)
target_link_libraries(int32search_range_test int32search m)

add_executable(int32search_bloom_test test/test_bloom.c)
target_compile_options(int32search_bloom_test PRIVATE -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG -DINT32_SEARCH_USE_BLOOM)
target_link_libraries(int32search_bloom_test int32search m)

add_executable(int32search_bench
    benchmark/bench_main.c
    benchmark/bench_data_gen.c)
target_compile_options(int32search_bench PRIVATE -mavx2)
target_link_libraries(int32search_bench int32search m)
