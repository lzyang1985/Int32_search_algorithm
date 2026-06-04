---
title: 需求对齐文档 — Int64 二期扩展 (Path B1 主线 + Bloom Bypass)
status: Draft
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Architect
task_id: task_005_int64_extension
parent_doc: none
parent_task: root
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/meetings/meeting_index/meeting_012_int64_feasibility/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_013_bloom_toggle/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_014_poc_design/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_015_poc_result_review/03_decisions.md"
  - "docs/decisions/poc_int64_report.md"
trace_code:
  - "src/poc_int64_avx2.c"
  - "src/poc_int64_b1.c"
  - "src/poc_int64_b1_crossover.c"
  - "src/poc_bloom_bypass.c"
tags: [alignment, int64, b1, bloom-bypass, phase1]
---

# 需求对齐文档 — Int64 二期扩展 (Path B1 主线 + Bloom Bypass)

## 1. 原始需求

### 1.1 项目目标

基于已有 libint32search（Int32 高性能查找库）的架构模式和工程经验，开发独立库 **libint64search**，提供 Int64 数据的精确查找能力。

### 1.2 技术路线核心（经 POC 验证，meeting_015 全票确认）

```
Path B1 (high20 dir + lo44 4路 cmpeq 扫描) → 主线
Scalar 二分 (标量 lower_bound)            → 回退/正确性基准
Bloom Bypass (_Atomic(int) 运行时切换)     → 集成
Path A AVX2 SIMD 二分                     → ❌ 已证伪 (0.58x 负加速)
```

### 1.3 关键决议链路

| 会议 | 决议 | 内容 |
|------|------|------|
| meeting_012 | D-093 | 独立库 libint64search，模式复制非宏泛化 |
| meeting_013 | D-098 | Bloom 旁路方案 C'（_Atomic(int) + setter） |
| meeting_014 | D-106 | 4 级 GATE 门控 POC 体系 |
| meeting_015 | D-109 | GATE-2 FAIL + GATE-3 PASS → 新 Go 路径（人工已签收 ✅） |
| meeting_015 | D-110 | Phase 1 = Path B1 主线 + Scalar Fallback + Bloom Bypass |
| meeting_015 | D-111 | B1 阈值经 crossover POC 校准 → **409**（交叉点 512 × 0.8） |

---

## 2. 边界确认（任务范围）

### 2.1 Phase 1 包含

#### 模块清单（基于 D-110 裁剪）

```
libint64search Phase 1:

Layer 1: 平台抽象层（复用）
├── platform_memory.c/h    — 复用（32 字节对齐分配）
├── platform_cpu.c/h       — 复用（CPUID 检测）
└── platform_thread.h      — 复用（原子操作封装）

Layer 2: 构建与选路层
├── build_sorted_int64.c   — 模式复制 build_sorted.c（int64_t 排序+校验）
├── build_dir_int64.c      — NEW: high20 目录构建（sign-flip + int32_t dir[1048577]）
└── build_decision_int64.c — NEW: 路径决策（阈值 409，回退到标量二分）

Layer 3: 查询引擎层
├── search_scalar_int64.c  — 模式复制 search_scalar.c（int64_t 标量二分）
└── search_b1_int64.c      — NEW: high20 dir + lo44 4路 cmpeq 扫描 + 每桶回退

Layer 4: API 层
├── api_int64.c            — 模式复制 api.c（create/find/destroy/rebuild）
├── internal_int64.h       — 独立 int64_search_impl_t
└── int64_search.h         — 独立公开头文件

扩展层:
├── bloom_filter.c/h       — 复用（XXH32/XXH64 升级待 P2）
└── bloom_bypass           — 模式复制 _Atomic(int) bloom_bypass（memory_order_relaxed）
```

#### API 完整对标

| int32 API | int64 API | 优先级 |
|-----------|----------|--------|
| `int32_search_create()` | `int64_search_create()` | P0 |
| `int32_search_find()` | `int64_search_find()` | P0 |
| `int32_search_destroy()` | `int64_search_destroy()` | P0 |
| `int32_search_rebuild()` | `int64_search_rebuild()` | P0 |
| `int32_search_version()` | `int64_search_version()` | P1 |
| `int32_search_set_bloom_bypass()` | `int64_search_set_bloom_bypass()` | P0 |
| `int32_search_get_bloom_bypass()` | `int64_search_get_bloom_bypass()` | P1（新增） |
| `int32_search_find_range()` | `int64_search_find_range()` | P2（Phase 3） |

### 2.2 Phase 1 明确不包含

| 排除项 | 说明 |
|--------|------|
| ❌ Path A AVX2 SIMD 二分 | POC 证伪（0.58x），永不存在 |
| ❌ AVX-512 探索 | P2，Phase 3 |
| ❌ Eytzinger 布局 | P2，Phase 3（标量二分优化候选） |
| ❌ COW 多线程 | Phase 1 单线程首发，COW 为后续阶段 |
| ❌ 动态目录大小 | 固定 1048577 entries (4MB) |
| ❌ int64 bloom XXH64 升级 | P2，Phase 1 复用 XXH32 |
| ❌ find_range 实现 | P2，Phase 3 |
| ❌ 代码与 int32 库共享 | 独立文件，模式复制 |

---

## 3. 需求理解（对现有项目的理解）

### 3.1 现有架构可复用部分

| 组件 | 复用方式 | 原因 |
|------|---------|------|
| `platform_memory.c/h` | 直接链接 | 32 字节对齐通用 |
| `platform_cpu.c/h` | 直接链接 | CPUID 检测通用 |
| `platform_thread.h` | 直接链接 | 原子操作封装通用 |
| `bloom_filter.c/h` | 直接链接 | 布隆过滤器与 key 宽度无关 |
| Bloom bypass `_Atomic(int)` | 模式复制 | POC 已验证 |
| `build_decision` 框架 | 代码参考 | 流程相同（参数不同） |
| 四层架构模式 | 模式复制 | D-020 分层、COW 模式、不透明句柄 |

### 3.2 不能复用的部分

| 组件 | 原因 |
|------|------|
| `search_avx2.c` | int64 AVX2 二分已被证伪 |
| `build_dir.c` | high16 (65536 桶) vs high20 (1M 桶) |
| `search_b1.c` | 8路 int32 cmpeq vs 4路 int64 cmpeq |
| `build_b1.c` | lo16 compact array (uint16_t) 在 int64 下不可用 |
| `int32_search_impl_t` | 需要独立 int64 版本 |

### 3.3 与 int32 的关键差异

| 维度 | int32 | int64 |
|------|-------|-------|
| 元素大小 | 4 bytes | 8 bytes |
| 10M 数据量 | 40 MB | 80 MB |
| B1 目录大小 | 256 KB (high16) | 4 MB (high20) |
| B1 桶内并行度 | 8路/16路 (`_mm256_cmpeq_epi32/16`) | 4路 (`_mm256_cmpeq_epi64`) |
| Sign-flip | 不需要 | 必须 `key ^ (1ULL<<63)` |
| 回退目标 | Path A AVX2 二分 | 标量二分（Path A 不存在） |
| B1 阈值 | 2000 (int32 crossover 校准) | 409 (int64 crossover POC 校准) |
| 库名 | libint32search | libint64search |
| 头文件 | `int32_search.h` | `int64_search.h` |

---

## 4. 疑问澄清

### 4.1 已澄清项

| # | 疑问 | 决议 | 来源 |
|---|------|------|------|
| 1 | Path A 被证伪后 Phase 1 范围如何调整 | B1 主线 + Scalar Fallback | D-110 |
| 2 | B1 阈值直接复用 int32=2000 是否可行 | 不可，经 crossover POC 校准为 409 | D-111 |
| 3 | 是否复用 int32_search_impl_t | 否，独立 int64_search_impl_t | D-110 |
| 4 | API 是否统一 | 独立库 libint64search，独立头文件和类型 | D-093 |
| 5 | bloom_bypass memory_order | `relaxed`（D-098 设计规范） | D-113 |
| 6 | dir 用 int32_t 还是 int64_t | int32_t (4MB)，n > 2^31 时断言拒绝 | D-111 |
| 7 | 回退粒度 (全局 vs 每桶) | 每桶回退，Phase 1 实现 | meeting_015 交叉讨论 |

### 4.2 待澄清项（需在 Architect 阶段解决）

| # | 疑问 | 优先级 |
|---|------|--------|
| 1 | COW 原子交换方案：`int64_b1_snapshot_t` (vals + dir 共 2 指针 + size_t，24 bytes)，x86-64 超出 16 字节 lock-free 上限，需 spinlock 还是引用计数？ | P1 |
| 2 | 错误码命名空间：`INT64_SEARCH_OK` vs 复用 `INT32_SEARCH_OK`？ | P1 |
| 3 | config_t 是否需要额外字段（强制路径选择、阈值覆盖）？ | P2 |
| 4 | `find_range` 在 B1 路径下的实现策略 | P2 |

---

## 5. 性能基线

### 5.1 B1 路径（主线）

| 分布 | 规模 | max_bucket | B1 cy/q | vs Scalar |
|------|------|-----------|------|-----------|
| uniform | 1M | 8 | 144 | **4.91x** |
| uniform | 10M | 26 | 318 | **5.30x** |
| skewed 80/20 | 1M | 16 | 150 | **4.43x** |
| skewed 80/20 | 10M | 71 | 443 | **4.08x** |
| zipf α=1.0 | 1M | 69,732 | 2036 → 回退 | → 676 |
| zipf α=1.0 | 10M | 692,681 | 29,917 → 回退 | → 1596 |

### 5.2 Scalar Fallback（回退路径）

10M uniform: **~1560 cy/q**（标量二分基线）

### 5.3 Bloom Bypass

- 5 项验证全部通过
- 并发安全（2 writer + 4 reader，2 秒无崩溃）
- Path B1 下 bloom 开销约占 70% 查询延迟，bypass 可显著提升"确定存在"场景性能

### 5.4 内存占用

| 组件 | 大小 |
|------|------|
| vals (10M int64) | 80 MB |
| dir[1048577] int32_t | 4 MB |
| bloom (可选) | ~1-2 MB |
| **总计** | **~85 MB** |

---

## 6. 关联信息

- **需求基线**：[总需求文档.md](../../requirements/总需求文档.md)
- **技术路线**：[技术路线.md](../../architecture/技术路线.md)
- **POC 结果**：[poc_int64_report.md](../../decisions/poc_int64_report.md)
- **前置会议**：meeting_012, meeting_013, meeting_014, meeting_015
- **POC 源代码**：
  - [src/poc_int64_avx2.c](../../../src/poc_int64_avx2.c)
  - [src/poc_int64_b1.c](../../../src/poc_int64_b1.c)
  - [src/poc_int64_b1_crossover.c](../../../src/poc_int64_b1_crossover.c)
  - [src/poc_bloom_bypass.c](../../../src/poc_bloom_bypass.c)
