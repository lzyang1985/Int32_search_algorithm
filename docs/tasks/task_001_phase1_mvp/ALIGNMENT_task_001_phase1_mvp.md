---
title: 需求对齐文档 — Phase 1 MVP (Path A 单路径)
status: Frozen
created_at: 2026-05-27
updated_at: 2026-06-01
author: Agent_Architect
task_id: task_001_phase1_mvp
parent_doc: none
parent_task: root
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/meetings/meeting_index/meeting_001_feasibility_review/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_002_adaptive_strategy_review/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_003_implementation_planning/03_decisions.md"
trace_code:
  - "src/poc_benchmark_v3.c"
tags: [alignment, requirements, phase1, mvp, path-a]
---

# 需求对齐文档 — Phase 1 MVP (Path A 单路径)

## 1. 原始需求

### 1.1 项目目标
开发高性能 C 语言库 **libint32search**，提供 Int32 数据的精确查找能力。利用 AVX2 SIMD 指令集实现远超标准二分查找的查询速度（10M 数据规模下 ≥ 3.5x 加速比）。

### 1.2 三阶段交付（meeting_003 D-023）
```
Phase 1: MVP — AVX2 二分（Path A 单路径）
  → Phase 2: v1.0 — A+B1 双路径 + COW
    → Phase 3: v1.1 — 扩展与跨平台
```

**本文档仅覆盖 Phase 1 MVP。**

---

## 2. 边界确认（任务范围）

### 2.1 MVP 包含（meeting_003 D-024）

| 模块 | 文件 | 功能 |
|------|------|------|
| 平台抽象层 | `platform_memory.c` | 32 字节对齐内存分配/释放（`_mm_malloc`/`_mm_free` 封装） |
| 平台抽象层 | `platform_cpu.c` | CPUID 运行时 SIMD 能力检测（AVX2 存在性检测） |
| 构建层 | `build_sorted.c` | 排序 + 数据完整性校验（qsort 排序，去重/单调性检查可选） |
| 查询引擎 | `search_scalar.c` | 标量二分查找（正确性黄金基准 + 非 AVX 平台回退） |
| 查询引擎 | `search_avx2.c` | AVX2 8 路块状 SIMD 二分查找（主力查询路径） |
| API 层 | `api.c` | `create`/`find`/`destroy` 统一入口 |
| API 层 | `internal.h` | 内部结构体定义（不安装） |
| API 层 | `include/int32_search.h` | 唯一公开头文件 |
| 构建系统 | `Makefile` | 三目标编译（lib/test/bench） |
| 构建系统 | `CMakeLists.txt` | CMake 辅助构建 |
| 构建系统 | `README.txt` | gcc 编译命令记录 |
| 测试 | `test/*.c` | 单元测试 + 正确性验证 |
| Benchmark | `benchmark/*.c` | 性能基准 |

### 2.2 MVP 明确不包含

| 不包含项 | 原因 | 归属阶段 |
|----------|------|----------|
| B1 路径（`build_dir.c`、`build_decision.c`、`build_b1.c`、`search_b1.c`） | 复杂性后移，MVP 聚焦单路径 | Phase 2 |
| COW 多线程更新（`platform_thread.c`） | 单线程 MVP 无需原子交换 | Phase 2 |
| 布隆过滤器 | 编译开关，非核心路径 | Phase 3 |
| SSE2/AVX-512 编译版本 | MVP 仅 AVX2 + 标量，多版本后移 | Phase 3 |
| `int32_search_rebuild()` API | COW 依赖 | Phase 2 |
| 范围查询接口 | 预留声明但不实现 | Phase 3 |
| Windows 移植 | 主力 Linux，Windows 后移 | Phase 3 |

### 2.3 MVP 阶段路径决策：硬编码 PATH_A

根据 meeting_003 D-024 和 meeting_002 D-015，MVP 阶段**不实现分布分析逻辑**。API 的内部实现将路径硬编码为 `PATH_A`。Phase 2 中才引入 `build_decision.c` 的自动选路。

---

## 3. 需求理解

### 3.1 核心算法：AVX2 8 路块状 SIMD 二分查找

基于 [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c#L114-L148) 中已验证的 `search_avx2_binary()` 算法：

```c
// 核心思想：在排序数组上做块状二分，每步加载 8 个连续元素
// 通过 _mm256_cmpgt_epi32 一次比较 8 个值，popcount 确定目标位置
// 当剩余区间 < 8 时退化为标量二分
```

关键行为：
- 当 `n < 8` 时直接走标量二分路径
- 块选取 `mid = lo + (hi - lo) / 2`，`block = mid & ~7`（向下对齐到 8）
- **必须修复**：`block = hi - 8` 的下溢风险（D-025 第 6 条）：当 `hi < 8` 时，`block = hi - 8` 会产生无符号溢出的巨大正数，导致越界读取

### 3.2 API 设计原则（meeting_001 D-007）

| 原则 | 具体实现 |
|------|----------|
| 不透明句柄 | `typedef void* int32_search_t;` |
| int 错误码 | `0` = 成功，`-1` = 未找到，`-2` = 空句柄，`-3` = 内存错误，`-4` = 无效参数 |
| 调用方管理内存 | `out_index` 由调用方提供指针；库不跨 FFI 边界分配 |
| 构建-查询分离 | `create` 一次性构建，`find` 只读查询 |

### 3.3 数据结构

MVP 阶段内部结构极简（仅 Path A）：

```c
// internal.h
typedef struct {
    int32_t  *vals;       // 排序数组，32 字节对齐
    size_t    n;          // 元素数量
    int       path;       // 硬编码 PATH_A (0)
} int32_search_impl_t;
```

### 3.4 命名与工程约定

| 约定 | 说明 |
|------|------|
| **命名法** | 下划线命名法（snake_case），文件名前缀表达层级：`platform_`、`build_`、`search_` |
| **编译** | `gcc -O3 -std=c11 -mavx2`，Makefile 主力 + README.txt 记录单行 gcc 命令 |
| **构建目标** | `make lib`（libint32search.a）、`make test`、`make bench` |
| **头文件** | `include/int32_search.h` 为唯一公开头文件；`src/internal.h` 不安装 |
| **安全** | 开发阶段默认 `-fsanitize=address,undefined` |

### 3.5 POC 算法已验证正确

基于三轮 POC benchmark（v1/v2/v3），算法正确性和性能已经验证：
- `search_avx2_binary()` 与 `bsearch()` 交叉验证正确
- 10M uniform 50% 命中率：~172 cycles/query（5.1x vs 标量二分）
- `n < 8` 退化路径正确处理

---

## 4. 疑问澄清

### 4.1 已澄清的问题

| 编号 | 问题 | 决议 | 来源 |
|------|------|------|------|
| Q1 | 是否需要动态插入/删除？ | 偶尔少量增删，以批量重建为主 | meeting_001 人工确认 |
| Q2 | 是否需要范围查询？ | 保留接口声明，不实现 | meeting_001 人工确认 |
| Q3 | AVX 加速是否必需？ | 速度为王，AVX 全面投入 | meeting_001 人工确认 |
| Q4 | 需不需要锚点索引？ | 不需要，POC 证实为负优化 | D-008/D-009 |
| Q5 | 需不需要自适应多路径（MVP 阶段）？ | 不需要，MVP 仅 Path A | D-024 |
| Q6 | 构建系统方案？ | Makefile 主 + CMake 辅 + README.txt | D-022/D-026 |

### 4.2 本阶段无待澄清问题

经过三轮会议（meeting_001/002/003），所有技术分歧已解决，无遗留待澄清问题。

---

## 5. 风险识别

| 风险 | 等级 | 说明 | 缓解 |
|------|------|------|------|
| `search_avx2.c` 的 `block=hi-8` 下溢 | **高** | 当 `hi < 8` 时 `block` 溢出为极大正数 | MVP 必须增加 `if (hi < 8) goto scalar_fallback` 保护 |
| AVX2 intrinsic 跨编译器行为差异 | 低 | GCC vs Clang 的 `_mm256_loadu_si256` 行为一致 | MVP 主力 GCC，Phase 3 验证 Clang/MSVC |
| `qsort()` 性能瓶颈 | 低 | 1000 万数据排序 ~300ms，可接受 | 未来可替换为基数排序 |
| 32 字节对齐在旧编译器不可用 | 低 | 需要 C11 `_Alignas` 或 GCC `__attribute__((aligned(32)))` | 回退到 `_mm_malloc` 直接使用 |

---

## 6. 关联信息

- 父文档：无（顶层任务，冷启动首个任务）
- 子任务：暂无
- 关联代码：
  - [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c#L114-L148) — AVX2 SIMD 二分核心算法
  - [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c#L10-L16) — 对齐内存分配模式
  - [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c#L65-L92) — high16 dir 构建与校验（Phase 2 参考）
