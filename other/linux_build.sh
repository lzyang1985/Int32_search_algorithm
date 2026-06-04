#!/bin/bash
set -e
cd /root/poc
rm -f src/*.o
gcc -O3 -std=c11 -Wall -Wextra -mavx2 -march=native -Isrc -c src/platform_memory.c -o src/platform_memory.o
gcc -O3 -std=c11 -Wall -Wextra -Isrc -c src/platform_cpu.c -o src/platform_cpu.o
gcc -O3 -std=c11 -Wall -Wextra -Isrc -c src/build_sorted.c -o src/build_sorted.o
gcc -O3 -std=c11 -Wall -Wextra -Isrc -c src/search_scalar.c -o src/search_scalar.o
gcc -O3 -std=c11 -Wall -Wextra -mavx2 -Isrc -c src/search_avx2.c -o src/search_avx2.o
gcc -O3 -std=c11 -Wall -Wextra -Iinclude -Isrc -c src/api.c -o src/api.o
echo "Phase 1 compiled OK"
gcc -O3 -std=c11 -mavx2 -march=native poc_benchmark_v3.c -o poc_benchmark_v3
echo "POC v3 compiled OK"
