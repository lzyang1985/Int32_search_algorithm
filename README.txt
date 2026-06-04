================================================================================
  Int32/Int64 高性能查找库 — 项目介绍与函数参考手册
  版本: v1.0.0 (Int32) / v0.1.0 (Int64) | 最后更新: 2026-06-02
================================================================================

===== 1. 项目概述 =====

本项目提供两个高性能 C 语言静态库，用于有序整数数组的精确查找：

  libint32search  — Int32 查找库 (主力, v1.0.0)
  libint64search  — Int64 查找库 (二期, v0.1.0)

核心目标是在 10M 数据规模下，利用 SIMD (AVX2) 指令集实现远超标准二分查找
的查询速度。两个库均采用纯 C (C11) 实现，零外部运行时依赖，仅需 GCC。

===== 1.5 ⚠️ 重要提示：本项目的核心前提 =====

本库的性能严重依赖以下两个关键因素，使用前务必理解：

1. 被查找数据的均匀性（Uniformity）

   Path B1 路径（本库最快路径）依赖 high-N-bit 目录索引实现 O(1) 定位。
   目录索引假定数据的高位分布是均匀的：
   - 如果数据分布均匀 → 目录桶大小受控，B1 高效命中，性能可达标量二分
     的 5x 以上
   - 如果数据分布严重倾斜（如大量聚集在少数 high-bit 段）→ 目录桶会极
     度膨胀，B1 退化甚至自动回退到标量二分，性能大幅下降

   因此，本库最适合：
   - 随机分布的整数主键（如数据库自增 ID 混入随机 UUID 哈希）
   - 哈希值查找
   - 均匀采样的数值索引
   不适合：
   - 严重倾斜的数据（如 Zipf 分布、大量重复值）

2. 布隆过滤器的合理开关

   Bloom filter 会在查找前用 O(1) 的布隆预筛快速拒绝不存在的 key，跳过
   昂贵的二分查找。但布隆本身也有恒定开销：
   - 高命中率场景（>80%）：建议关闭布隆。因为大部分查询都会命中，布隆
     只是多走一趟确认，反而拖慢速度
   - 低命中率场景（<50%）：建议开启布隆。大量 miss 被 O(1) 过滤，避免
     每次 miss 都走完 O(logN) 二分

   实际 Benchmark 参考（本机 5M Int32 数据，10K 查询）：

   | 场景              | Bloom | 命中率 | 吞吐量       | 说明           |
   |-------------------|-------|--------|-------------|----------------|
   | 混合负载（默认）  | ❌ OFF | 50%    | 3.58 M/s    | miss 走满二分   |
   | 混合负载          | ✅ ON  | 50%    | 8.03 M/s    | 布隆快速过滤miss|
   | 全命中            | ❌ OFF | 100%   | 16.92 M/s   | 布隆纯开销,关掉最快 |

   上面三种场景最高和最低之间差距达 4.7 倍。请根据实际命中率预期
   通过 int32_search_config_t.use_bloom 选择合适的策略。

   Int64 库还提供了运行时动态开关: int64_search_set_bloom_bypass()。

===== 2. 架构概览 =====

2.1 查找路径

  库在构建时对数据分布进行分析，自动选择最优查找路径：

  Path B1  — high20 目录 O(1) 定位 + lo44 标量等值扫描
             适用: 数据分布均匀、桶大小可控（默认 ≤ 409）
             Int64 平台: 318 cy/query (5.30x vs Scalar)

  Path Scalar — 标准二分查找（黄金正确性基准）
             适用: 倾斜数据、B1 不适用时自动回退
             始终可用，作为 fallback

  Path A    — AVX2 8 路块状 SIMD 二分 (仅 Int32)
             适用: 中大规模均匀数据
             性能: 10M ~172 cy/query (3.5x-5.1x vs Scalar)

2.2 四层模块架构

  Layer 1: 平台抽象层 (PAL)
  ├── platform_memory.c/h  — 32 字节对齐内存分配
  ├── platform_cpu.c/h     — CPUID 运行时 SIMD 能力检测
  └── platform_thread.h    — 原子操作封装 (COW 多线程)

  Layer 2: 构建与选路层
  ├── build_sorted.c       — 排序 + 数据完整性校验
  ├── build_dir.c/h        — high20 目录构建 + 一致性校验
  ├── build_decision.c/h   — 分布分析 + 路径决策
  └── build_b1.c/h         — B1 路径 lo16 数组构建

  Layer 3: 查询引擎层
  ├── search_scalar.c/h    — 标量二分 (正确性基准)
  ├── search_avx2.c/h      — AVX2 8 路块状 SIMD 二分 (仅 Int32)
  └── search_b1.c/h        — B1 high20 dir + lo16 扫描

  Layer 4: 公开 API 层
  ├── api.c / api_int64.c  — create/find/destroy/rebuild 统一入口
  ├── internal.h           — 内部结构体 (不暴露)
  └── internal_int64.h     — Int64 内部结构体

  扩展层
  ├── bloom_filter.c/h     — 布隆过滤器 (编译宏可选)
  └── xxhash/              — xxHash 哈希库

2.3 并发模型: 构建-查询分离 + COW (Copy-On-Write)

  Writer: 创建新版本 → COW 原子交换 → 旧版本等待读者完成后延迟回收
  Reader: 读取当前快照 → 零锁查询

  Int32 COW:
  - Path A: _Atomic 单指针交换
  - Path B1: _Atomic 三指针原子交换 (vals + lo16 + dir)

===== 3. libint32search API 参考 =====

公开头文件: include/int32_search.h

3.1 数据类型与常量

  错误码:
  #define INT32_SEARCH_OK              0   操作成功
  #define INT32_SEARCH_ERR_NOT_FOUND  -1   未找到目标值
  #define INT32_SEARCH_ERR_NULL_HANDLE -2  无效句柄 (NULL)
  #define INT32_SEARCH_ERR_MEMORY      -3   内存分配失败
  #define INT32_SEARCH_ERR_INVALID_ARG -4  无效参数

  不透明句柄:
  typedef void* int32_search_t;

  构建配置:
  typedef struct {
      int use_bloom;      启用布隆过滤器 (1=启用, 0=禁用)
      int reserved[7];    ABI 兼容预留 (必须填 0)
  } int32_search_config_t;

3.2 函数列表

3.2.1 int32_search_create — 创建查找引擎

  声明:
    int32_search_t int32_search_create(const int32_t *data, size_t n,
                                        const int32_search_config_t *cfg);

  功能:
    输入一个 int32 数组，内部完成排序、分布分析和索引构建，
    自动选择最优查找路径 (Path A 或 Path B1)，返回不透明句柄。

  参数:
    data    — 原始 int32 数据数组 (无需预先排序)
    n       — 数据元素个数
    cfg     — 构建配置，传 NULL 使用默认配置

  返回值:
    成功: 不透明句柄 (非 NULL)
    失败: NULL (data 为空 / n=0 / 内存不足)

  示例:
    #include "int32_search.h"

    int32_t data[] = {42, 7, 99, 3, 15, 8};
    size_t n = sizeof(data) / sizeof(data[0]);

    int32_search_t h = int32_search_create(data, n, NULL);
    if (h == NULL) {
        fprintf(stderr, "create failed\n");
        return 1;
    }

  注意:
    - 传入的原数组不会被修改 (库内部排序副本)
    - 库内部使用 32 字节对齐分配，适合 SIMD 操作
    - cfg->use_bloom=1 仅在编译时定义了 INT32_SEARCH_USE_BLOOM 时生效

3.2.2 int32_search_find — 精确查找

  声明:
    int int32_search_find(int32_search_t handle, int32_t key,
                          size_t *out_index);

  功能:
    在已构建的查找结构中查找目标 key，返回第一个匹配元素的索引。

  参数:
    handle    — create 返回的句柄
    key       — 要查找的 int32 目标值
    out_index — [输出] 匹配元素在排序数组中的索引

  返回值:
    INT32_SEARCH_OK          — 找到，*out_index 为有效索引
    INT32_SEARCH_ERR_NOT_FOUND  — 未找到
    INT32_SEARCH_ERR_NULL_HANDLE — handle 为 NULL
    INT32_SEARCH_ERR_INVALID_ARG — out_index 为 NULL

  示例:
    size_t idx;
    int32_t key = 42;
    int rc = int32_search_find(h, key, &idx);
    if (rc == INT32_SEARCH_OK) {
        printf("found at index %zu\n", idx);
    } else if (rc == INT32_SEARCH_ERR_NOT_FOUND) {
        printf("%d not found\n", key);
    }

  线程安全:
    查询路径零锁，多线程可并发调用 find 不阻塞。

3.2.3 int32_search_destroy — 销毁查找引擎

  声明:
    int int32_search_destroy(int32_search_t handle);

  功能:
    释放构建阶段分配的所有内存 (排序数组、目录、布隆过滤器等)。

  参数:
    handle — create 返回的句柄

  返回值:
    INT32_SEARCH_OK  — 始终成功 (NULL 句柄也安全)

  示例:
    int32_search_destroy(h);
    h = NULL;

  注意:
    - destroy 会等待所有进行中的查询完成后再释放内存
    - 传入 NULL 是安全的 (幂等)
    - 销毁后句柄不可再用

3.2.4 int32_search_rebuild — 数据重建 (COW)

  声明:
    int int32_search_rebuild(int32_search_t handle,
                              const int32_t *data, size_t n);

  功能:
    替换句柄内的数据集。采用 COW (Copy-On-Write) 方式:
    构建新版本 → 原子交换指针 → 旧版本延迟回收。
    rebuild 不阻塞正在进行的查询。

  参数:
    handle  — create 返回的句柄
    data    — 新的 int32 数据数组
    n       — 新数据元素个数

  返回值:
    INT32_SEARCH_OK           — 成功
    INT32_SEARCH_ERR_NULL_HANDLE   — handle 为 NULL
    INT32_SEARCH_ERR_INVALID_ARG   — data 为空或 n=0
    INT32_SEARCH_ERR_MEMORY        — 内存分配失败

  示例:
    int32_t new_data[] = {100, 200, 300, 400};
    size_t new_n = sizeof(new_data) / sizeof(new_data[0]);

    int rc = int32_search_rebuild(h, new_data, new_n);
    if (rc != INT32_SEARCH_OK) {
        fprintf(stderr, "rebuild failed: %d\n", rc);
    }
    // 旧数据的查询可以继续进行，完成后自动回收

3.2.5 int32_search_find_range — 范围查找

  声明:
    int int32_search_find_range(int32_search_t handle, int32_t low,
                                 int32_t high, size_t *out_first,
                                 size_t *out_count);

  功能:
    查找排序数组中值在 [low, high] 区间内的所有元素。

  参数:
    handle     — create 返回的句柄
    low        — 区间下界 (含)
    high       — 区间上界 (含)
    out_first  — [输出] 区间内第一个元素在排序数组中的索引
    out_count  — [输出] 区间内元素个数

  返回值:
    INT32_SEARCH_OK            — 找到匹配区间
    INT32_SEARCH_ERR_NOT_FOUND   — 无元素在区间内 (*out_count=0)
    INT32_SEARCH_ERR_NULL_HANDLE — handle 为 NULL
    INT32_SEARCH_ERR_INVALID_ARG — out_first/out_count 为 NULL 或 low>high

  示例:
    size_t first, count;
    int rc = int32_search_find_range(h, 100, 500, &first, &count);
    if (rc == INT32_SEARCH_OK) {
        printf("range [100,500]: first idx=%zu, count=%zu\n", first, count);
    }

3.2.6 int32_search_version — 获取版本号

  声明:
    const char *int32_search_version(void);

  返回值:
    版本字符串 "libint32search 1.0.0"

  示例:
    printf("library version: %s\n", int32_search_version());


===== 4. libint64search API 参考 =====

公开头文件: include/int64_search.h

4.1 数据类型与常量

  错误码:
  #define INT64_SEARCH_OK              0
  #define INT64_SEARCH_ERR_NOT_FOUND  -1
  #define INT64_SEARCH_ERR_NULL_HANDLE -2
  #define INT64_SEARCH_ERR_MEMORY      -3
  #define INT64_SEARCH_ERR_INVALID_ARG -4
  #define INT64_SEARCH_ERR_TOO_LARGE   -5   数据规模超过 INT32_MAX

  不透明句柄:
  typedef void* int64_search_t;

  构建配置:
  typedef struct {
      int use_bloom;      启用布隆过滤器
      int reserved[7];    ABI 兼容预留
  } int64_search_config_t;

4.2 函数列表

4.2.1 int64_search_create — 创建查找引擎

  声明:
    int64_search_t int64_search_create(const int64_t *data, size_t n,
                                        const int64_search_config_t *cfg);

  功能:
    同 int32_search_create，处理 int64 数据。自动选择 Path B1 或
    Path Scalar（Int64 无 Path A）。

  参数:
    data  — 原始 int64 数据数组
    n     — 数据元素个数 (≤ INT32_MAX)
    cfg   — 构建配置，传 NULL 使用默认配置

  返回值:
    成功: 不透明句柄
    失败: NULL

4.2.2 int64_search_find — 精确查找

  声明:
    int int64_search_find(int64_search_t handle, int64_t key,
                           size_t *out_index);

  功能:
    查找 int64 key，返回匹配索引。

  返回值:
    INT64_SEARCH_OK / INT64_SEARCH_ERR_NOT_FOUND /
    INT64_SEARCH_ERR_NULL_HANDLE

4.2.3 int64_search_destroy — 销毁查找引擎

  声明:
    int int64_search_destroy(int64_search_t handle);

  功能:
    释放所有资源。NULL 句柄安全。

4.2.4 int64_search_rebuild — 数据重建

  声明:
    int int64_search_rebuild(int64_search_t handle,
                              const int64_t *data, size_t n);

  功能:
    替换数据集，同 int32_search_rebuild。

  返回值:
    INT64_SEARCH_OK / INT64_SEARCH_ERR_NULL_HANDLE /
    INT64_SEARCH_ERR_INVALID_ARG / INT64_SEARCH_ERR_MEMORY /
    INT64_SEARCH_ERR_TOO_LARGE

  ⚠️ 线程安全: Int64 rebuild 当前仅支持单线程调用。
    rebuild 期间其他线程并发 find 存在数据竞争风险。
    若需多线程并发 rebuild，请在外部使用互斥锁保护 rebuild 调用。

4.2.5 int64_search_version — 获取版本号

  声明:
    const char *int64_search_version(void);

  返回值:
    "libint64search 0.1.0"

4.2.6 int64_search_set_bloom_bypass — 设置布隆旁路开关

  声明:
    int int64_search_set_bloom_bypass(int64_search_t handle, int bypass);

  功能:
    运行时动态开关布隆过滤器。bypass=1 时跳过布隆预筛，
    bypass=0 时启用布隆预筛。线程安全 (_Atomic 存储)。

  参数:
    handle  — create 返回的句柄
    bypass  — 0=启用布隆, 非 0=跳过布隆

  返回值:
    INT64_SEARCH_OK / INT64_SEARCH_ERR_NULL_HANDLE

4.2.7 int64_search_get_bloom_bypass — 获取布隆旁路状态

  声明:
    int int64_search_get_bloom_bypass(int64_search_t handle);

  返回值:
    0 (启用) / 1 (旁路) / 负数错误码 (INT64_SEARCH_ERR_NULL_HANDLE)

4.2.8 int64_search_find_range — 范围查找 (预留)

  声明:
    int int64_search_find_range(int64_search_t handle, int64_t low,
                                 int64_t high, size_t *out_first,
                                 size_t *out_count);

  状态:
    接口已声明，当前返回 INT64_SEARCH_ERR_NOT_FOUND (待实现)。


===== 5. 完整使用示例 =====

5.1 Int32 基本用法

  #include <stdio.h>
  #include <stdint.h>
  #include "int32_search.h"

  int main(void) {
      int32_t data[] = {42, 7, 99, 3, 15, 8, 27, 64, 1, 50};
      size_t n = sizeof(data) / sizeof(data[0]);

      int32_search_t h = int32_search_create(data, n, NULL);
      if (h == NULL) { return 1; }

      int32_t targets[] = {42, 100, 15, -1};
      size_t nt = sizeof(targets) / sizeof(targets[0]);
      for (size_t i = 0; i < nt; i++) {
          size_t idx;
          int rc = int32_search_find(h, targets[i], &idx);
          if (rc == INT32_SEARCH_OK)
              printf("found %d at index %zu\n", targets[i], idx);
          else
              printf("%d not found\n", targets[i]);
      }

      int32_search_destroy(h);
      return 0;
  }

5.2 Int64 基本用法

  #include <stdio.h>
  #include <stdint.h>
  #include "int64_search.h"

  int main(void) {
      int64_t data[] = {INT64_C(1000000000000),
                        INT64_C(500000000000)};
      size_t n = sizeof(data) / sizeof(data[0]);

      int64_search_t h = int64_search_create(data, n, NULL);
      if (h == NULL) { return 1; }

      size_t idx;
      int rc = int64_search_find(h, INT64_C(500000000000), &idx);
      printf("rc=%d, idx=%zu\n", rc, idx);

      int64_search_destroy(h);
      return 0;
  }

5.3 多线程 rebuild 示例 (Int32)

  #include <pthread.h>
  #include "int32_search.h"

  int32_search_t g_handle;

  void* reader_thread(void *arg) {
      (void)arg;
      for (int i = 0; i < 100000; i++) {
          size_t idx;
          int32_search_find(g_handle, rand(), &idx);
      }
      return NULL;
  }

  void* writer_thread(void *arg) {
      (void)arg;
      int32_t new_data[] = {10, 20, 30, 40, 50};
      int32_search_rebuild(g_handle, new_data, 5);
      return NULL;
  }

  int main(void) {
      int32_t init_data[] = {1, 2, 3, 4, 5};
      g_handle = int32_search_create(init_data, 5, NULL);

      pthread_t readers[4], writer;
      for (int i = 0; i < 4; i++)
          pthread_create(&readers[i], NULL, reader_thread, NULL);
      pthread_create(&writer, NULL, writer_thread, NULL);

      for (int i = 0; i < 4; i++)
          pthread_join(readers[i], NULL);
      pthread_join(writer, NULL);

      int32_search_destroy(g_handle);
      return 0;
  }

5.4 布隆过滤器用法 (Int64, 需编译宏)

  编译时定义 INT64_SEARCH_USE_BLOOM 启用:

  int64_search_config_t cfg = { .use_bloom = 1 };
  int64_search_t h = int64_search_create(data, n, &cfg);

  // 运行时临时关闭布隆 (调试用)
  int64_search_set_bloom_bypass(h, 1);

  // 重新启用
  int64_search_set_bloom_bypass(h, 0);
  printf("bloom bypass: %d\n", int64_search_get_bloom_bypass(h));


===== 6. 性能数据 =====

> ⚠️ 以下数据基于均匀分布数据测得。数据分布和布隆开关对性能有巨大影响，
> 详见"1.5 重要提示"章节。

测试平台: Intel Xeon Gold 6152 @ 2.10GHz, 10M uniform, 50% 命中率

6.1 libint64search

  Path B1 (high20 + lo44 scan): ~318 cy/query  (5.30x vs Scalar)
  Path Scalar (fallback):       ~1560 cy/query

  性能回归门禁: test_int64_perf.c | 10M uniform | ≤5000 cy/query | 0 mismatches

6.2 libint32search

  Path A (AVX2 8 路 SIMD 二分):
    1M 数据: ~146 cy/query
    5M 数据: ~146 cy/query
    10M 数据: ~172 cy/query  (3.5x-5.1x vs 标量二分)

  Path B1 (high16 dir + lo16 SIMD 扫描):
    1M 数据: ~75 cy/query   (2.1x vs Path A)

  crossover 点约 max_bucket=2000 (B1 在桶大小≤2000 时胜 A)

  Scalar (fallback): ~884 cy/query @ 10M


===== 7. 编译与集成 =====

7.1 编译 libint32search (Windows MinGW Shell)

  :: Platform layer
  gcc -O3 -std=c11 -c src/platform_memory.c -o src/platform_memory.o
  gcc -O3 -std=c11 -c src/platform_cpu.c -o src/platform_cpu.o

  :: Build layer
  gcc -O3 -std=c11 -c src/build_sorted.c -o src/build_sorted.o
  gcc -O3 -std=c11 -c src/build_dir.c -o src/build_dir.o
  gcc -O3 -std=c11 -c src/build_b1.c -o src/build_b1.o
  gcc -O3 -std=c11 -c src/build_decision.c -o src/build_decision.o

  :: Search layer + API
  gcc -O3 -std=c11 -c src/search_scalar.c -o src/search_scalar.o
  gcc -O3 -std=c11 -mavx2 -c src/search_avx2.c -o src/search_avx2.o
  gcc -O3 -std=c11 -mavx2 -c src/search_b1.c -o src/search_b1.o
  gcc -O3 -std=c11 -Iinclude -Isrc -c src/api.c -o src/api.o

  :: 可选: 布隆过滤器
  gcc -O3 -std=c11 -DINT32_SEARCH_USE_BLOOM -c src/bloom_filter.c -o src/bloom_filter.o

  :: 打包静态库
  ar rcs libint32search.a src/*.o

7.2 编译 libint64search (Windows MinGW Shell)

  gcc -O3 -std=c11 -c -Iinclude -Isrc src/platform_memory.c -o src/platform_memory.o
  gcc -O3 -std=c11 -c -Iinclude -Isrc src/build_sorted_int64.c -o src/build_sorted_int64.o
  gcc -O3 -std=c11 -c -Iinclude -Isrc src/search_scalar_int64.c -o src/search_scalar_int64.o
  gcc -O3 -std=c11 -c -Iinclude -Isrc -mavx2 src/build_dir_int64.c -o src/build_dir_int64.o
  gcc -O3 -std=c11 -c -Iinclude -Isrc src/build_decision_int64.c -o src/build_decision_int64.o
  gcc -O3 -std=c11 -c -Iinclude -Isrc -mavx2 src/search_b1_int64.c -o src/search_b1_int64.o
  gcc -O3 -std=c11 -c -Iinclude -Isrc src/api_int64.c -o src/api_int64.o
  ar rcs libint64search.a src/platform_memory.o src/build_sorted_int64.o src/search_scalar_int64.o src/build_dir_int64.o src/build_decision_int64.o src/search_b1_int64.o src/api_int64.o

7.3 集成到用户程序

  :: 仅 Int32 库
  gcc -O3 -std=c11 -mavx2 -o myapp myapp.c -Iinclude -L. -lint32search -lm

  :: 仅 Int64 库
  gcc -O3 -std=c11 -mavx2 -o myapp myapp.c -Iinclude -L. -lint64search -lm

  :: 同时使用 Int32 + Int64
  gcc -O3 -std=c11 -mavx2 -o myapp myapp.c -Iinclude -L. -lint32search -lint64search -lm

7.4 Makefile 方式 (Linux/Mac, 支持 ASan/UBSan)

  make lib          编译静态库 libint32search.a
  make test         编译并运行测试 (含 ASan/UBSan)
  make bench        编译并运行 benchmark
  make test-thread  编译并运行多线程安全测试 (ThreadSanitizer)
  make test-b1      编译并运行 B1 路径测试 (ASan/UBSan)
  make test-range   编译并运行 find_range 测试 (ASan/UBSan)
  make clean        清理所有产物

  :: Int64 目标
  make lib-int64       编译静态库 libint64search.a
  make test-int64      编译并运行 Int64 正确性测试 (ASan/UBSan)
  make test-int64-perf 编译并运行 Int64 10M 性能回归测试
  make test-int64-zipf 编译并运行 Int64 Zipf 退化场景测试
  make clean-int64     清理 Int64 编译产物

7.5 编译演示程序

  :: Int32 查找 Demo (默认, 含布隆)
  gcc -O3 -std=c11 -mavx2 demo/demo_search.c -Iinclude -L. -lint32search -lm -o demo/demo_search.exe

  :: Int32 查找 Demo (显式关闭布隆)
  gcc -O3 -std=c11 -mavx2 demo/demo_int32_no_bloom.c -Iinclude -L. -lint32search -lm -o demo/demo_int32_no_bloom.exe

  :: Int64 查找 Demo
  gcc -O3 -std=c11 -mavx2 demo/demo_int64_search.c -Iinclude -L. -lint64search -lm -o demo/demo_int64_search.exe

  :: Int32 查找 Demo (关闭布隆, 100% 命中)
  gcc -O3 -std=c11 -mavx2 demo/demo_int32_no_bloom_fullhit.c -Iinclude -L. -lint32search -lm -o demo/demo_int32_no_bloom_fullhit.exe


===== 8. 测试与验证 =====

8.1 编译测试

  :: Int32 单元测试
  gcc -O3 -std=c11 -mavx2 test/test_unit.c src/*.o -Iinclude -Isrc -o test_unit
  ./test_unit

  :: Int64 正确性测试
  gcc -O3 -std=c11 -mavx2 test/test_int64.c src/platform_memory.o src/build_sorted_int64.o src/search_scalar_int64.o src/build_dir_int64.o src/build_decision_int64.o src/search_b1_int64.o src/api_int64.o -Iinclude -Isrc -o test_int64 -lm
  ./test_int64

  :: Int64 10M 性能回归测试 (10M 数据, ~10 秒)
  gcc -O3 -std=c11 -mavx2 test/test_int64_perf.c src/platform_memory.o src/build_sorted_int64.o src/search_scalar_int64.o src/build_dir_int64.o src/build_decision_int64.o src/search_b1_int64.o src/api_int64.o -Iinclude -Isrc -o test_int64_perf -lm
  ./test_int64_perf

  :: Int64 Zipf α=1.0 退化场景测试 (验证 B1 fallback)
  gcc -O3 -std=c11 -mavx2 test/test_int64_zipf.c src/platform_memory.o src/build_sorted_int64.o src/search_scalar_int64.o src/build_dir_int64.o src/build_decision_int64.o src/search_b1_int64.o src/api_int64.o -Iinclude -Isrc -o test_int64_zipf -lm
  ./test_int64_zipf

  :: 范围查找测试
  gcc -O3 -std=c11 -mavx2 test/test_range.c src/*.o -Iinclude -Isrc -o test_range -lm
  ./test_range

  :: 布隆过滤器测试 (需编译宏)
  gcc -O3 -std=c11 -mavx2 -DINT32_SEARCH_USE_BLOOM test/test_bloom.c src/*.o \
      -Iinclude -Isrc -o test_bloom -lm
  ./test_bloom

  :: Fuzz 测试
  gcc -O3 -std=c11 -mavx2 test/test_fuzz.c src/*.o -Iinclude -Isrc -o test_fuzz
  ./test_fuzz

8.2 Benchmark

  gcc -O3 -std=c11 -mavx2 benchmark/bench_main.c src/*.o -Iinclude -Isrc -o bench_main
  ./bench_main

8.3 多线程安全测试

  gcc -O3 -std=c11 -mavx2 -fsanitize=thread test/test_thread.c src/*.o -Iinclude -Isrc -o test_thread
  ./test_thread


===== 9. 错误处理模式 =====

所有 API 函数遵循统一错误处理模式:

  int32_search_t h = int32_search_create(data, n, NULL);
  if (h == NULL) {
      // create 失败 (data 为空 / 内存不足)
      return 1;
  }

  size_t idx;
  int rc = int32_search_find(h, key, &idx);
  switch (rc) {
  case INT32_SEARCH_OK:
      printf("found at %zu\n", idx);
      break;
  case INT32_SEARCH_ERR_NOT_FOUND:
      printf("not found\n");
      break;
  case INT32_SEARCH_ERR_NULL_HANDLE:
  case INT32_SEARCH_ERR_INVALID_ARG:
      printf("invalid usage\n");
      break;
  }

  int32_search_destroy(h);

严格检查:
  - create 必须检查返回值是否为 NULL
  - find 必须检查返回码是否为 INT32_SEARCH_OK
  - destroy 可安全传入 NULL (幂等)
  - 销毁后不要再使用句柄


===== 10. 平台与编译器支持 =====

  编译器: GCC ≥ 8.0 (主力), MSVC + Clang 通过 CMake 支持
  平台:   Linux (主力), Windows (MinGW)
  标准:   C11 (_Alignas, _Atomic, stdatomic.h)
  指令集: 运行时 CPUID 检测 → 编译时多版本 (标量 → AVX2)

  命名规范: 下划线命名法 (snake_case)
  API 风格: 不透明句柄 void* + int 错误码 + 调用方管理内存

  已知限制:
  - Windows MinGW 下 AVX2 Path A 性能退化 (0.45x-0.55x vs Linux 5.26x),
    Path B1 和 Path Scalar 不受影响
  - Int64 库数据规模限制 n ≤ INT32_MAX
  - Int64 Path B1 内部目录固定 1M 条目 (与数据规模无关的 8MB 开销)

===== 项目链接 =====

  总需求文档:    docs/requirements/总需求文档.md
  技术路线文档:  docs/architecture/技术路线.md
  全局索引:      docs/README.md
  源代码目录:    src/
  公开头文件:    include/int32_search.h  include/int64_search.h
  测试目录:      test/
  Benchmark:     benchmark/
  演示程序:      demo/demo_search.c  demo/demo_int32_no_bloom.c  demo/demo_int32_no_bloom_fullhit.c  demo/demo_int64_search.c
