---
title: 最终共识文档 — Phase 1 MVP (Path A 单路径)
status: Frozen
created_at: 2026-05-27
updated_at: 2026-06-01
author: Agent_Architect
task_id: task_001_phase1_mvp
parent_doc: "docs/tasks/task_001_phase1_mvp/ALIGNMENT_task_001_phase1_mvp.md"
parent_task: task_001_phase1_mvp
source_docs:
  - "docs/tasks/task_001_phase1_mvp/ALIGNMENT_task_001_phase1_mvp.md"
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
tags: [consensus, requirements, phase1, mvp, path-a]
---

# 最终共识文档 — Phase 1 MVP (Path A 单路径)

## 1. 明确的需求描述

### 1.1 核心交付物

实现一个纯 C11 的 Int32 查找静态库 `libint32search.a`，提供以下三个公开 API：

| API | 功能 | 说明 |
|-----|------|------|
| `int32_search_create` | 构建查询索引 | 输入 Int32 数组，内部排序后构建 Path A 查询上下文 |
| `int32_search_find` | 精确查找 | 使用 AVX2 8 路块状 SIMD 二分查找，返回匹配元素索引 |
| `int32_search_destroy` | 销毁资源 | 释放 build 阶段分配的所有内存 |

### 1.2 功能需求覆盖

| 编号 | 需求 | 覆盖方式 |
|------|------|----------|
| FR-01 | 批量数据构建 | `int32_search_create()` — 输入数组 → 排序 → 查询上下文 |
| FR-02 | 精确查找 | `int32_search_find()` — AVX2 SIMD 二分 + 标量回退 |
| FR-03 | 资源销毁 | `int32_search_destroy()` — 幂等释放 |
| FR-04 | 数据重建 | **Phase 2** — `int32_search_rebuild()` |
| FR-05 | 范围查询接口 | **Phase 3** — 头文件中预留声明 |
| FR-06 | 布隆过滤器 | **Phase 3** — `#ifdef USE_BLOOM_FILTER` |

> MVP 阶段交付 FR-01/02/03，FR-04/05/06 延后。

### 1.3 非功能需求覆盖

| 编号 | 需求 | MVP 承诺 |
|------|------|----------|
| NFR-01 | 查询延迟（10M uniform, 50% hit）< 200 cy/q | POC 实测 ~172 cy/q，Benchmark 回归验证 |
| NFR-02 | 内存占用（10M）≤ 40 MB | 单排序数组 40MB，无额外索引 |
| NFR-03 | SIMD 加速比 ≥ 3.5x @ 10M | POC 实测 5.1x，Benchmark 回归验证 |
| NFR-04 | 数据分布敏感度 < 5% | Path A 对分布完全不敏感（0.3%） |
| SR-01 | SIMD 边界安全 n=0~64 | 全覆盖测试矩阵 |
| SR-02 | COW 内存序 | Phase 2（MVP 无 COW） |
| SR-03 | dir 一致性校验 | Phase 2（MVP 无 dir） |
| SR-04 | 内存泄漏防护 | create 失败资源回滚 + destroy 幂等 |
| SR-05 | ASan/UBSan 零告警 | 开发阶段默认开启 |
| CR-01 | GCC ≥ 8.0 编译 | 主力编译环境 |
| CR-02 | C11 标准 | `_Alignas`、`stdint.h` |
| CR-03 | 运行时 CPUID | `platform_cpu.c` 检测 AVX2 |
| ER-01 | Makefile + CMakeLists.txt + README.txt | 三件套 |
| ER-02 | 下划线命名法 | 全域 snake_case |
| ER-03 | 不透明句柄 + 错误码 | D-007 完全遵守 |
| ER-04 | 代码量 ~600 行（MVP 子集） | 含测试/benchmark |

---

## 2. 技术实现方案

### 2.1 模块架构

```
include/int32_search.h          — 公开 API 声明 + 错误码

src/
├── internal.h                  — 内部结构体 int32_search_impl_t
├── platform_memory.c           — 32 字节对齐内存 (NULL/resize 处理暂用断言)
├── platform_cpu.c              — CPUID AVX2 检测 (__builtin_cpu_supports)
├── build_sorted.c              — 排序 + 校验 (qsort)
├── search_scalar.c             — 标量二分 (黄金基准)
├── search_avx2.c               — AVX2 8 路块状二分 (主力)
└── api.c                       — create/find/destroy

test/
├── test_unit.c                 — 单元测试
├── test_correctness.c          — bsearch() 交叉验证
└── test_boundary.c             — n=0~64 边界测试

benchmark/
├── bench_main.c                — 性能基准
└── bench_data_gen.c            — 测试数据生成

Makefile / CMakeLists.txt / README.txt
```

### 2.2 查询算法伪代码

```
search_avx2_find(vals, n, target):
    if n == 0: return NOT_FOUND
    lo = 0, hi = n

    while hi - lo >= 8:
        mid = lo + (hi - lo) / 2
        block = mid & ~7                  // 向下对齐到 8
        if block > hi - 8: block = hi - 8 // 防下溢

        key = _mm256_set1_epi32(target)
        vec = _mm256_loadu_si256(&vals[block])
        cmp = _mm256_cmpgt_epi32(key, vec)
        mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp))
        le_count = popcount(mask ^ 0xFF)  // target > vals[block+i] 的个数

        if le_count == 8:      lo = block + 8
        elif le_count == 0:    hi = block
        else:
            if vals[block + le_count] == target: return block + le_count
            lo = block + le_count
            hi = block + le_count

    // 尾部标量二分 (hi - lo < 8)
    while lo < hi:
        mid = lo + (hi - lo) / 2
        if vals[mid] < target: lo = mid + 1
        else: hi = mid
    return (lo < n && vals[lo] == target) ? lo : NOT_FOUND
```

### 2.3 构建流程

```
int32_search_create(data, n, cfg):
    1. 参数校验: n > 0, data != NULL
    2. 分配 impl 结构体
    3. 分配 vals 数组 (32 字节对齐), memcpy
    4. qsort(vals, n, 4, cmp)
    5. 数据校验: 无重复? 单调性? (可配置)
    6. 初始化 handle 各字段
    7. 返回 handle
    ── 任何步骤失败 → 回滚已分配资源 → 返回 NULL
```

### 2.4 技术约束确认

| 约束 | 来源 | MVP 实现 |
|------|------|----------|
| 纯 C11，无 C++ | 宪法级 | `gcc -std=c11` |
| 最小依赖（仅 xxHash + 标准库） | 技术路线 | xxHash 不链接（MVP 无用） |
| 32 字节对齐所有 SIMD 缓冲区 | D-006 | `_mm_malloc(size, 32)` |
| `block=hi-8` 下溢保护 | D-025-6 | `if (block > hi - 8) block = hi - 8` |
| 查询路径无锁 | D-005 | MVP 单线程天然满足 |
| 调用方管理输出内存 | D-007 | `out_index` 为传入指针 |
| 不暴露内部结构体 | D-007 | `typedef void* int32_search_t` |

---

## 3. 任务边界限制

### 3.1 MVP 硬边界

| 维度 | 限制 | 说明 |
|------|------|------|
| **路径** | 仅 Path A | 硬编码 `PATH_A`，不实现分布分析 |
| **线程** | 单线程 | 无 C11 atomic / pthread |
| **SIMD** | AVX2 + 标量 | 不含 SSE2/AVX-512 |
| **API** | create/find/destroy | 不含 rebuild/range/bloom |
| **数据结构** | 单排序数组 | 无 dir/lo16 辅助数组 |
| **平台** | Linux/GCC | Windows 验证非强制 |
| **代码量** | ~600 行（业务代码） | 不含 test/benchmark |

### 3.2 明确的"不做"清单

1. **不实现 B1 路径** -- Phase 2
2. **不实现 COW 多线程** -- Phase 2
3. **不实现分布分析** -- Phase 2
4. **不实现 `rebuild` API** -- Phase 2
5. **不实现布隆过滤器** -- Phase 3
6. **不编译 SSE2/AVX-512 版本** -- Phase 3
7. **不实现范围查询逻辑** -- Phase 3（仅头文件预留声明）
8. **不做 Windows 移植** -- Phase 3

---

## 4. 验收标准

### 4.1 编译验收

- [ ] `gcc -O3 -std=c11 -mavx2` 编译通过，零警告（`-Wall -Wextra`）
- [ ] `make lib` 产出 `libint32search.a`
- [ ] `make test` 产出 `int32search_test` 并全通过
- [ ] `make bench` 产出 `int32search_bench` 并正常执行
- [ ] `-fsanitize=address,undefined` 编译零告警

### 4.2 正确性验收

- [ ] 100 万次随机查询结果与标准 `bsearch()` 完全一致
- [ ] n=0~64 每个值通过搜索正确性测试（边界矩阵）
- [ ] n=0 正确返回 NOT_FOUND
- [ ] n=1 命中/不命中均正确
- [ ] n=7（<8 块大小）直接走标量路径，正确
- [ ] 重复元素：返回第一个匹配索引

### 4.3 性能验收

- [ ] 10M uniform 50% 命中率：< 200 cycles/query（目标 ~172 cy/q）
- [ ] 10M 数据 AVX2 加速比 vs 标量二分：≥ 3.5x
- [ ] n < 8 小数据无性能退化

### 4.4 API 契约验收

- [ ] `int32_search_create(NULL, 0, NULL)` 返回 `NULL`
- [ ] `int32_search_find(NULL, 0, &out)` 返回 `INT32_SEARCH_ERR_NULL_HANDLE`
- [ ] `int32_search_destroy(NULL)` 不崩溃（幂等）
- [ ] 正常查找命中返回 `INT32_SEARCH_OK`，`out_index` 指向正确位置
- [ ] 正常查找不命中返回 `INT32_SEARCH_ERR_NOT_FOUND`
- [ ] `out_index` 为 `NULL` 时返回 `INT32_SEARCH_ERR_INVALID_ARG`

### 4.5 安全验收

- [ ] ASan 零告警（无堆栈越界、无 use-after-free）
- [ ] UBSan 零告警（无符号溢出、无对齐问题）
- [ ] `create` 中途失败时所有已分配内存正确释放
- [ ] `destroy` 两次调用不崩溃

---

## 5. 不确定性确认

**所有技术决策已在三轮会议中达成共识，无遗留不确定性。**

| 决策域 | 最终方案 | 共识达成会议 |
|--------|----------|------------|
| 核心算法 | AVX2 8 路块状 SIMD 二分（Path A） | D-008（meeting_001） |
| 数据结构 | 排序数组，零额外索引 | D-001（meeting_001） |
| API 风格 | 不透明句柄 + int 错误码 | D-007（meeting_001） |
| 模块划分 | 四层架构 | D-020（meeting_003） |
| 构建系统 | Makefile + CMake + README.txt | D-022/D-026（meeting_003） |
| MVP 范围 | 仅 Path A | D-024（meeting_003） |
| 安全策略 | 安全左移（ASan/UBSan 默认开启） | D-025（meeting_003） |
| B1 路径 | Phase 2 实现 | D-014（meeting_002） |

---

## 6. 关联信息

- 父文档：[ALIGNMENT_task_001_phase1_mvp.md](ALIGNMENT_task_001_phase1_mvp.md)
- 子任务：暂无（待 TASK 文档拆分后创建）
- 后续文档：DESIGN_task_001_phase1_mvp.md、TASK_task_001_phase1_mvp.md
