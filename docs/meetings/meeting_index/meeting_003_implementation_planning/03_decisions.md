---
title: 决议记录 — 实施方案讨论会
meeting_id: meeting_003_implementation_planning
status: Frozen
created_at: 2026-05-27
updated_at: 2026-05-27
---

# 决议记录：实施方案讨论会

## 总决议

**全体 5 位 Agent 一致确认：采用四层模块结构 + 单仓库三编译目标 + 三阶段交付计划。MVP = AVX2 SIMD 二分（Path A），B1 路径进入 Phase 2。**

---

## 议题 1：模块划分

### 决议 D-020：四层模块架构

**决议**：采用如下四层模块划分方案。（5/5 通过）

```
Int32 搜索算法库
│
├── Layer 1: 平台抽象层 (PAL)
│   ├── platform_memory.c/h      — 跨平台对齐内存分配 (aligned_alloc 封装)
│   ├── platform_cpu.c/h         — CPUID 运行时检测 + SIMD 能力查询
│   └── platform_thread.c/h      — 平台无关原子操作 (atomic_store_ptr/load_ptr)
│
├── Layer 2: 构建与选路层
│   ├── build_sorted.c           — 排序 + 数据校验（A/B1 共用基础）
│   ├── build_dir.c              — high16 directory 构建 + 一致性校验（含 D-016 修复）
│   ├── build_decision.c         — D-015 分布分析 + 路径决策引擎（纯函数）
│   └── build_b1.c               — B1 路径构建：lo16 分配/填充
│
├── Layer 3: 查询引擎层
│   ├── search_scalar.c          — 标量二分（黄金基准 + 非 AVX 平台回退）
│   ├── search_avx2.c            — AVX2 8 路块状 SIMD 二分（A 路径主力）
│   └── search_b1.c              — high16 dir O(1) + lo16 SIMD 扫描（B1 路径）
│
├── Layer 4: 公开 API 层
│   ├── api.c                    — create/search/destroy/rebuild 统一入口
│   └── internal.h               — 内部结构体（不透明句柄实现，不安装）
│
└── 扩展层（编译开关控制）
    ├── bloom_filter.c/h         — 布隆过滤器 (#ifdef USE_BLOOM_FILTER)
    └── xxhash/                  — 已有哈希库，不动
```

**公开头文件**：`include/int32_search.h`（唯一用户可见头文件）

**内部头文件**：`src/internal.h`（不安装到系统 include 路径）

**文件命名规范**：下划线命名法（用户习惯），文件名前缀表达层级归属，避免深目录嵌套。

**设计理由**：
- 代码量预估 ~1000 行，细粒度子目录过度工程化
- 前缀命名提供足够的层级可见性（`search_*`、`build_*`、`platform_*`）
- P0 MVP 仅涉及 Layer 1-4 中约 6 个文件，其余按阶段递增

**表决**：5/5 通过。

---

### 决议 D-021：SIMD 多版本编译策略

**决议**：同一源文件通过编译时宏分段，AVX2 主力先行。（5/5 通过）

```c
// search_avx2.c 内部结构
#if defined(INT32_SEARCH_AVX512)
  size_t search_avx2_binary_avx512(...) { /* 16路并行 */ }
#elif defined(INT32_SEARCH_AVX2)
  size_t search_avx2_binary_avx2(...)   { /* 8路并行 */ }
#elif defined(INT32_SEARCH_SSE2)
  size_t search_avx2_binary_sse2(...)   { /* 4路并行 */ }
#endif
size_t search_avx2_binary_scalar(...)   { /* 标量回退，无条件编译 */ }
```

**编译方式**：同一源文件编译多次，每次传入不同宏和对应的 `-march` 选项。

**分阶段引入**：
- Phase 1 (MVP)：仅标量 + AVX2 两个版本
- Phase 3 (扩展)：补充 SSE2 + AVX-512 版本

**表决**：5/5 通过。

---

## 议题 2：子项目拆分

### 决议 D-022：单仓库三编译目标

**决议**：不拆分为多个独立仓库或子项目。采用单仓库内三编译目标。（5/5 通过）

```
int32_search_algorithm/
├── include/
│   └── int32_search.h            — 公开头文件
├── src/
│   ├── internal.h                — 内部头文件
│   ├── *.c                       — Layer 1-4 源文件
│   └── xxhash/                   — 已有，不动
├── test/
│   ├── test_unit.c               — 单元测试（链接 libint32search）
│   ├── test_integration.c        — 集成测试
│   └── test_correctness.c        — 大规模正确性验证
├── benchmark/
│   ├── bench_main.c              — 性能基准入口（链接 libint32search）
│   └── bench_data_gen.c          — 测试数据生成
├── Makefile                      — 三目标编译
├── CMakeLists.txt                — CMake 构建（可选）
├── README.txt                    — gcc 编译命令记录
└── docs/                         — 已有，不动
```

**三编译目标**：

| 目标 | 产物 | 编译命令示例 |
|------|------|-------------|
| `lib` | `libint32search.a` | `make lib` |
| `test` | `int32search_test` | `make test` |
| `bench` | `int32search_bench` | `make bench` |

**设计理由**：
- 代码量小（~1000 行），拆分仓库的管理成本远超收益
- Benchmark 链接核心库而非内联算法，自动验证 API 契约
- 用户只需链接 `libint32search.a` + `#include "int32_search.h"`

**表决**：5/5 通过。

---

## 议题 3：实施顺序

### 决议 D-023：四阶段交付计划（2026-05-27 修订：拆分原 Phase 2 COW 为独立的 Phase 1.5）

**决议**：采用四阶段渐进式交付。（5/5 通过）

```
Phase 1: MVP — AVX2 二分（Path A 单路径 + 单线程）
    ├── 标量二分（黄金基准）
    ├── AVX2 8 路块状 SIMD 二分
    ├── 基本 API（create/search/destroy）
    ├── 单元测试 + 正确性验证（与 bsearch() 交叉验证）
    └── Benchmark 回归（10M ~172 cy/query）
    │
    ▼
Phase 1.5: 多线程 — Path A COW
    ├── platform_thread.c（原子操作封装）
    ├── api.c 增加 rebuild() COW 接口
    ├── Path A 单指针原子交换（_Atomic const int32_t*）
    ├── 旧版本延迟回收
    └── ThreadSanitizer 零告警
    │
    ▼
Phase 2: v1.0 — A+B1 双路径
    ├── high16 dir 构建 + 一致性校验（D-016）
    ├── D-015 分布分析 + 路径决策
    ├── B1 路径实现（lo16 SIMD 扫描）
    ├── B1 COW struct 级三指针原子交换（D-017）
    ├── A vs B1 交叉验证
    └── 全规模 benchmark 回归
    │
    ▼
Phase 3: v1.1 — 扩展与跨平台
    ├── SSE2 编译版本
    ├── AVX-512 编译版本（#ifdef USE_AVX512）
    ├── 布隆过滤器（#ifdef USE_BLOOM_FILTER）
    ├── Windows 移植（Win32 thread）
    └── 范围查询接口（预留，标注 reserved）
```

**表决**：5/5 通过。

---

### 决议 D-024：MVP 范围边界

**决议**：MVP 仅包含 AVX2 SIMD 二分（Path A）+ 标量二分回退。B1 路径、COW、布隆过滤器均不在 MVP 范围。（5/5 通过）

**MVP 包含**：
- 排序数组构建（`build_sorted.c`）
- AVX2 8 路块状 SIMD 二分（`search_avx2.c`）
- 标量二分参考实现（`search_scalar.c`）
- 32 字节对齐内存分配（`platform_memory.c`）
- CPUID 检测（`platform_cpu.c`）
- 公开 API（`api.c` + `include/int32_search.h`）
- 内部头文件（`src/internal.h`）

**MVP 不包含**：
- B1 路径（`build_dir.c`、`build_decision.c`、`build_b1.c`、`search_b1.c`）
- COW 多线程更新（`platform_thread.c`）
- 布隆过滤器
- SSE2/AVX-512 编译版本

**MVP 验收标准**：
1. `gcc -O3 -std=c11 -mavx2` 编译通过
2. API 符合 D-007（不透明句柄 + 错误码 + 内存所有权）
3. 正确性与标准 `bsearch()` 一致（100 万次随机查询交叉验证）
4. 10M 数据 ~172 cy/query（与 POC v3 一致）
5. `n < 8` 退化路径正确处理

**表决**：5/5 通过。

---

### 决议 D-025：安全左移策略

**决议**：安全检查与核心代码同步开发，不推迟到审计阶段。（5/5 通过）

**具体措施**：
1. **标量引用先行**：`search_scalar.c` 作为所有 SIMD 版本的正确性黄金基准，最先实现
2. **ASan+UBSan 默认开启**：开发阶段编译配置必须包含 `-fsanitize=address,undefined`
3. **dir_validate 强制门控**：`build_dir.c` 中的一致性校验不仅在 debug 模式运行，生产构建也保留
4. **COW 内存序明确**：Phase 2 实现 COW 时必须显式指定 `memory_order_release`/`memory_order_acquire`
5. **SIMD 边界测试矩阵**：n=0~64 每个值必须通过搜索正确性测试
6. **关键修复**：`search_avx2.c` 中 `block = hi - 8` 增加下溢保护（当 hi < 8 时直接走标量路径）

**表决**：5/5 通过。

---

### 决议 D-026：文件与构建系统

**决议**：
1. 主构建系统：**Makefile**（符合用户习惯的 gcc 直接编译）
2. 辅助构建系统：**CMakeLists.txt**（为 CI/跨平台提供标准方案）
3. 编译命令记录：**README.txt**（记录 gcc 单行编译命令）
4. 命名规范：**下划线命名法**（用户习惯），文件名前缀表达层级

**表决**：5/5 通过。

---

## 与 meeting_001/002 决议关系

| 前序决议 | 影响 |
|----------|------|
| D-006（构建系统） | D-026 确认 Makefile + CMake + README.txt 方案 |
| D-007（API 设计） | D-024 MVP 承诺严格遵守不透明句柄 + 错误码 + 内存所有权 |
| D-008（方案 A 唯一路径） | MVP 阶段仅实现 Path A，B1 为 Phase 2 增强 |
| D-013（进入立项阶段） | 本次会议决议直接作为立项工作流的输入 |
| D-015（选路规则） | Phase 2 中实现，规则不变：max_sz>0.1n→A, max_bucket≤150→B1, else→A |
| D-016（prev_h bug） | Phase 2 build_dir.c 中修复 |
| D-017（COW struct 原子交换） | Phase 2 中实现 |

---

## 会议后续流程

```
meeting_003（本次会议）✅ 5/5 全部通过
        │
        ▼
生成总需求文档 + 技术路线文档（宪法级）
        │
        ▼
立项工作流 (project-initiation.md)
  ├── Align:    ALIGNMENT_xxx.md
  ├── Architect: DESIGN_xxx.md
  └── Atomize:   TASK_xxx.md
        │
        ▼
Execute (Phase 1 MVP)
```

---

## 附录：实施顺序一览

| 阶段 | 模块 | 文件 | P级 |
|------|------|------|-----|
| **Phase 1** | 标量二分 | `search_scalar.c` | P0 |
| | AVX2 二分 | `search_avx2.c` | P0 |
| | 内存分配 | `platform_memory.c` | P0 |
| | CPU 检测 | `platform_cpu.c` | P0 |
| | 数据排序 | `build_sorted.c` | P0 |
| | 公开 API | `api.c`、`include/int32_search.h`、`internal.h` | P0 |
| | 单元测试 | `test/*.c` | P0 |
| | Benchmark | `benchmark/*.c` | P0 |
| | 构建系统 | `Makefile`、`CMakeLists.txt`、`README.txt` | P0 |
| **Phase 1.5** | 线程封装 | `platform_thread.c` | P0 |
| | COW rebuild | `api.c`（扩展 rebuild 接口） | P0 |
| | 并发测试 | `test_concurrent.c` | P0 |
| **Phase 2** | dir 构建+校验 | `build_dir.c` | P1 |
| | 路径决策 | `build_decision.c` | P1 |
| | B1 构建 | `build_b1.c` | P1 |
| | B1 搜索 | `search_b1.c` | P1 |
| | B1 COW 并发 | `b1_snapshot` struct 级原子交换 | P1 |
| **Phase 3** | 布隆过滤器 | `bloom_filter.c` | P2 |
| | AVX-512 版本 | `search_avx2.c` (扩展 ifdef) | P2 |
| | SSE2 版本 | `search_avx2.c` (扩展 ifdef) | P2 |
| | Windows 移植 | `platform_*.c` (扩展 ifdef) | P2 |
