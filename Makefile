CC       = gcc
CFLAGS   = -O3 -std=c11 -Wall -Wextra
AR       = ar
ARFLAGS  = rcs

INCDIR   = include
SRCDIR   = src
TESTDIR  = test
BENCHDIR = benchmark

LIB_NAME    = libint32search
LIB64_NAME  = libint64search

SRCS     = $(SRCDIR)/platform_memory.c \
           $(SRCDIR)/platform_cpu.c \
           $(SRCDIR)/build_sorted.c \
           $(SRCDIR)/build_dir.c \
           $(SRCDIR)/build_b1.c \
           $(SRCDIR)/build_decision.c \
           $(SRCDIR)/search_scalar.c \
           $(SRCDIR)/search_b1.c \
           $(SRCDIR)/api.c

AVX2_SRC = $(SRCDIR)/search_avx2.c

OBJS     = $(SRCDIR)/platform_memory.o \
           $(SRCDIR)/platform_cpu.o \
           $(SRCDIR)/build_sorted.o \
           $(SRCDIR)/build_dir.o \
           $(SRCDIR)/build_b1.o \
           $(SRCDIR)/build_decision.o \
           $(SRCDIR)/search_scalar.o \
           $(SRCDIR)/search_avx2.o \
           $(SRCDIR)/search_b1.o \
           $(SRCDIR)/bloom_filter.o \
           $(SRCDIR)/api.o

OBJS64   = $(SRCDIR)/platform_memory.o \
           $(SRCDIR)/build_sorted_int64.o \
           $(SRCDIR)/search_scalar_int64.o \
           $(SRCDIR)/build_dir_int64.o \
           $(SRCDIR)/build_decision_int64.o \
           $(SRCDIR)/search_b1_int64.o \
           $(SRCDIR)/api_int64.o

.PHONY: all lib test test-thread test-b1 test-thread-b1 test-range test-bloom bench clean
.PHONY: lib-int64 test-int64 clean-int64

all: lib test bench

lib: $(LIB_NAME).a

$(LIB_NAME).a: $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(SRCDIR)/platform_memory.o: $(SRCDIR)/platform_memory.c $(SRCDIR)/platform_memory.h
	$(CC) -c $(CFLAGS) -I$(SRCDIR) $< -o $@

$(SRCDIR)/platform_cpu.o: $(SRCDIR)/platform_cpu.c $(SRCDIR)/platform_cpu.h
	$(CC) -c $(CFLAGS) -I$(SRCDIR) $< -o $@

$(SRCDIR)/build_sorted.o: $(SRCDIR)/build_sorted.c $(SRCDIR)/build_sorted.h $(SRCDIR)/platform_memory.h
	$(CC) -c $(CFLAGS) -I$(SRCDIR) $< -o $@

$(SRCDIR)/build_dir.o: $(SRCDIR)/build_dir.c $(SRCDIR)/build_dir.h
	$(CC) -c $(CFLAGS) -I$(SRCDIR) $< -o $@

$(SRCDIR)/build_b1.o: $(SRCDIR)/build_b1.c $(SRCDIR)/build_b1.h
	$(CC) -c $(CFLAGS) -I$(SRCDIR) $< -o $@

$(SRCDIR)/build_decision.o: $(SRCDIR)/build_decision.c $(SRCDIR)/build_decision.h $(SRCDIR)/internal.h
	$(CC) -c $(CFLAGS) -I$(SRCDIR) $< -o $@

$(SRCDIR)/search_scalar.o: $(SRCDIR)/search_scalar.c $(SRCDIR)/search_scalar.h
	$(CC) -c $(CFLAGS) -I$(SRCDIR) $< -o $@

$(SRCDIR)/search_avx2.o: $(SRCDIR)/search_avx2.c $(SRCDIR)/search_avx2.h
	$(CC) -c $(CFLAGS) -mavx2 -I$(SRCDIR) $< -o $@

$(SRCDIR)/search_b1.o: $(SRCDIR)/search_b1.c $(SRCDIR)/search_b1.h $(SRCDIR)/internal.h
	$(CC) -c $(CFLAGS) -mavx2 -I$(SRCDIR) $< -o $@

$(SRCDIR)/bloom_filter.o: $(SRCDIR)/bloom_filter.c $(SRCDIR)/bloom_filter.h
	$(CC) -c $(CFLAGS) -DINT32_SEARCH_USE_BLOOM -I$(SRCDIR) $< -o $@

# ── Int64 Library ──

lib-int64: $(LIB64_NAME).a

$(LIB64_NAME).a: $(OBJS64)
	$(AR) $(ARFLAGS) $@ $^

$(SRCDIR)/build_sorted_int64.o: $(SRCDIR)/build_sorted_int64.c $(SRCDIR)/platform_memory.h
	$(CC) -c $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) $< -o $@

$(SRCDIR)/search_scalar_int64.o: $(SRCDIR)/search_scalar_int64.c $(SRCDIR)/internal_int64.h
	$(CC) -c $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) $< -o $@

$(SRCDIR)/build_dir_int64.o: $(SRCDIR)/build_dir_int64.c $(SRCDIR)/internal_int64.h $(SRCDIR)/platform_memory.h
	$(CC) -c $(CFLAGS) -mavx2 -I$(INCDIR) -I$(SRCDIR) $< -o $@

$(SRCDIR)/build_decision_int64.o: $(SRCDIR)/build_decision_int64.c $(SRCDIR)/internal_int64.h
	$(CC) -c $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) $< -o $@

$(SRCDIR)/search_b1_int64.o: $(SRCDIR)/search_b1_int64.c $(SRCDIR)/internal_int64.h
	$(CC) -c $(CFLAGS) -mavx2 -I$(INCDIR) -I$(SRCDIR) $< -o $@

$(SRCDIR)/api_int64.o: $(SRCDIR)/api_int64.c $(INCDIR)/int64_search.h $(SRCDIR)/internal_int64.h \
                        $(SRCDIR)/platform_memory.h $(SRCDIR)/bloom_filter.h
	$(CC) -c $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) $< -o $@

test-int64: $(LIB64_NAME).a $(TESTDIR)/test_int64.c
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_int64.c $(LIB64_NAME).a -o int64search_test -lm
	@echo "Running Int64 tests..."
	./int64search_test

clean-int64:
	rm -f $(SRCDIR)/build_sorted_int64.o $(SRCDIR)/search_scalar_int64.o
	rm -f $(SRCDIR)/build_dir_int64.o $(SRCDIR)/build_decision_int64.o
	rm -f $(SRCDIR)/search_b1_int64.o $(SRCDIR)/api_int64.o
	rm -f $(LIB64_NAME).a int64search_test

$(SRCDIR)/api.o: $(SRCDIR)/api.c $(INCDIR)/int32_search.h $(SRCDIR)/internal.h \
                  $(SRCDIR)/platform_memory.h $(SRCDIR)/platform_cpu.h \
                  $(SRCDIR)/platform_thread.h \
                  $(SRCDIR)/build_sorted.h $(SRCDIR)/build_dir.h \
                  $(SRCDIR)/build_b1.h $(SRCDIR)/build_decision.h \
                  $(SRCDIR)/search_scalar.h $(SRCDIR)/search_avx2.h \
                  $(SRCDIR)/search_b1.h
	$(CC) -c $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) $< -o $@

test: $(LIB_NAME).a $(TESTDIR)/test_unit.c $(TESTDIR)/test_correctness.c $(TESTDIR)/test_boundary.c
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_unit.c $(LIB_NAME).a -o int32search_test -lm
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_correctness.c $(LIB_NAME).a -o int32search_correctness -lm
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_boundary.c $(LIB_NAME).a -o int32search_boundary -lm
	@echo "Running unit tests..."
	./int32search_test
	@echo "Running correctness tests..."
	./int32search_correctness
	@echo "Running boundary tests..."
	./int32search_boundary
	@echo "Running scalar fallback tests..."
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_scalar_fallback.c $(LIB_NAME).a -o int32search_scalar_fallback -lm
	./int32search_scalar_fallback

bench: $(LIB_NAME).a $(BENCHDIR)/bench_main.c $(BENCHDIR)/bench_data_gen.c
	$(CC) $(CFLAGS) -mavx2 -I$(INCDIR) -I$(SRCDIR) \
		$(BENCHDIR)/bench_main.c $(BENCHDIR)/bench_data_gen.c $(LIB_NAME).a -o int32search_bench -lm

test-force-avx2: $(LIB_NAME).a $(TESTDIR)/test_unit.c $(TESTDIR)/test_correctness.c $(TESTDIR)/test_boundary.c
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG -DINT32SEARCH_FORCE_AVX2 \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_unit.c $(LIB_NAME).a -o int32search_test_force_avx2 -lm
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG -DINT32SEARCH_FORCE_AVX2 \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_correctness.c $(LIB_NAME).a -o int32search_correctness_force_avx2 -lm
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG -DINT32SEARCH_FORCE_AVX2 \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_boundary.c $(LIB_NAME).a -o int32search_boundary_force_avx2 -lm
	@echo "Running unit tests (AVX2 forced)..."
	./int32search_test_force_avx2
	@echo "Running correctness tests (AVX2 forced)..."
	./int32search_correctness_force_avx2
	@echo "Running boundary tests (AVX2 forced)..."
	./int32search_boundary_force_avx2

test-thread: $(LIB_NAME).a $(TESTDIR)/test_thread.c
	$(CC) $(CFLAGS) -fsanitize=thread -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_thread.c $(LIB_NAME).a \
		-o int32search_thread_test -lm
	./int32search_thread_test

test-b1: $(LIB_NAME).a $(TESTDIR)/test_b1_correctness.c $(TESTDIR)/test_b1_boundary.c $(TESTDIR)/test_b1_decision.c
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_b1_correctness.c $(LIB_NAME).a \
		-o int32search_b1_correctness_test -lm
	./int32search_b1_correctness_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_b1_boundary.c $(LIB_NAME).a \
		-o int32search_b1_boundary_test -lm
	./int32search_b1_boundary_test
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_b1_decision.c $(LIB_NAME).a \
		-o int32search_b1_decision_test -lm
	./int32search_b1_decision_test

test-thread-b1: $(LIB_NAME).a $(TESTDIR)/test_thread_b1.c
	$(CC) $(CFLAGS) -fsanitize=thread -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_thread_b1.c $(LIB_NAME).a \
		-o int32search_thread_b1_test -lm
	./int32search_thread_b1_test

test-range: $(LIB_NAME).a $(TESTDIR)/test_range.c
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_range.c $(LIB_NAME).a \
		-o int32search_range_test -lm
	./int32search_range_test

test-bloom: $(LIB_NAME).a $(TESTDIR)/test_bloom.c
	$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG -DINT32_SEARCH_USE_BLOOM \
		-I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_bloom.c $(LIB_NAME).a \
		-o int32search_bloom_test -lm
	./int32search_bloom_test

clean:
	rm -f $(SRCDIR)/*.o $(LIB_NAME).a
	rm -f int32search_test int32search_correctness int32search_boundary int32search_bench
	rm -f int32search_test_force_avx2 int32search_correctness_force_avx2 int32search_boundary_force_avx2
	rm -f int32search_scalar_fallback test_scalar_fallback
	rm -f int32search_thread_test
	rm -f int32search_b1_correctness_test int32search_b1_boundary_test int32search_b1_decision_test
	rm -f int32search_thread_b1_test
	rm -f int32search_range_test int32search_bloom_test
	rm -f $(TESTDIR)/*.o $(BENCHDIR)/*.o
