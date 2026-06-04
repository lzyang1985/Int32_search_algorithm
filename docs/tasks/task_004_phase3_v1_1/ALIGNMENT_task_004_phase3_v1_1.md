---
title: ALIGNMENT — Phase 3 v1.1 扩展与跨平台
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-02
author: Agent_Architect
task_id: task_004_phase3_v1_1
parent_task: root
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/meetings/meeting_index/meeting_011_phase2_audit_review/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_011_phase2_audit_review/04_action_items.md"
tags: [phase3, v1.1, alignment]
---

# ALIGNMENT — Phase 3 v1.1 扩展与跨平台

## 1. 原始需求（来源：总需求文档 §5）

```
Phase 3: v1.1 — 扩展与跨平台
  ├── FR-05 范围查询接口（预留）
  ├── FR-06 布隆过滤器
  ├── SSE2 / AVX-512 编译版本
  └── Windows 平台移植
```

### 1.1 功能需求追溯

| 编号 | 功能 | 优先级 | 当前状态 |
|------|------|--------|----------|
| FR-05 | 范围查询 `find_range()` | P2 | 接口已声明，实现为 stub（返回 `ERR_INVALID_ARG`） |
| FR-06 | 布隆过滤器预筛 | P2 | 零代码，编译开关 `#ifdef USE_BLOOM_FILTER` 预留 |
| — | SSE2 编译版本 | P3（D-087 调整） | 零代码，技术路线设计为 `INT32_SEARCH_SSE2` 宏 |
| — | AVX-512 编译版本 | P3（D-087 调整） | 零代码，技术路线设计为 `INT32_SEARCH_AVX512` 宏 |
| — | Windows 平台移植 | — | 部分完成：README.txt 有 MinGW 命令；`platform_thread_yield()` 为空实现 |

### 1.2 非功能需求追溯（总需求文档 §3）

| 编号 | 需求 | Phase 3 影响 |
|------|------|-------------|
| SR-01 | SIMD 边界安全 | find_range 实现需保持 n=0~64 无越界 |
| SR-02 | COW 内存序 | find_range 并发查询时与 rebuild 不冲突 |
| SR-03 | dir 一致性校验 | 已有，Phase 3 不修改 |
| SR-04 | 内存泄漏防护 | 布隆过滤器新增分配需纳入回滚逻辑 |
| SR-05 | ASan/UBSan 零告警 | 新代码需通过 |
| CR-01 | GCC ≥ 8.0 | 不变 |
| CR-02 | Linux + Windows | Windows 移植是本阶段目标 |
| CR-03 | C11 | 不变 |
| CR-04 | 多指令集 | SSE2/AVX-512 是本阶段目标 |

---

## 2. 当前代码基线

### 2.1 find_range 现状

接口已在 [int32_search.h:L94-L108](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/include/int32_search.h#L94-L108) 完整声明，语义为：

> 查找 [low, high] 闭区间内第一个元素下标 (`out_first`) 及区间内元素个数 (`out_count`)
> 等价于两次二分查找（lower_bound + upper_bound）

实现位于 [api.c:L253-L263](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L253-L263)，当前为 stub：

```c
int int32_search_find_range(...) {
    (void)handle; (void)low; (void)high;
    (void)out_first; (void)out_count;
    return INT32_SEARCH_ERR_INVALID_ARG;
}
```

**关键问题**：find_range 需要在 Path A（AVX2 二分）和 Path B1（dir + lo16 扫描）两条路径上都工作。技术路线 §5 "排序数组天然支持范围查询"——这指的是 Path A 的已排序 `vals[]` 天然支持二分 lower_bound/upper_bound。B1 路径需要特殊处理。

### 2.2 布隆过滤器现状

- xxHash 已存在于 `src/xxhash/` 目录但**未链入**主线
- API 层 `internal.h` 中 `int32_search_impl_t` 预留了 `void *bloom` 字段
- `int32_search_config_t` 有 `int reserved[8]`（可改为 `int use_bloom`）
- 技术路线设计中 `int32_search_config_t.use_bloom` 字段存在
- 无任何 bloom 相关编译或运行时代码

### 2.3 SSE2/AVX-512 现状

- 技术路线 §4.1 设计了同一源文件 `search_avx2.c` 通过宏分段的方案
- D-087 已将 SSE2（TODO-08）和 AVX-512（TODO-09）优先级从 P2 降为 P3
- 当前无任何 SSE2/AVX-512 条件编译路径

### 2.4 Windows 移植现状

| 维度 | 现状 |
|------|------|
| 编译命令 | README.txt 有完整 MinGW 命令（Git Bash/MSYS2） |
| AVX2 性能 | meeting_005 确认 MinGW 下 AVX2 退化（0.45x-0.55x vs 标量），技术路线 §7 列为已知风险 |
| 线程 | `platform_thread_yield()` 为空实现（[platform_thread.h:L17-L19](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/platform_thread.h#L17-L19)） |
| 原子操作 | 使用 C11 `stdatomic.h`，MinGW 支持 |
| Makefile | 仅含 gcc 命令，无 Windows native 构建 |
| 残留物 | `src/search_avx2_win.s`（调查用汇编输出） |

### 2.5 meeting_011 P2/P3 并行项

meeting_011 决议 D-086 明确：**Phase 3 启动条件仅为 C1-C2（P1），C3-C5（P2）及 P3 项在 Phase 3 第一波中并行推进**。

| 行动项 | 内容 | 优先级 | Phase 3 关联 |
|--------|------|--------|-------------|
| ACT-03 | `platform_thread_yield()` → `_mm_pause()` | P2 | Windows 移植基础 |
| ACT-04 | ACCEPTANCE 补充偏差 A/B/C | P2 | 仅文档 |
| ACT-05 | README.txt/头文件注释补充 | P2 | 仅文档 |
| ACT-06 | TODO 优先级更新 | P2 | 仅文档 |
| ACT-07 | FINAL 标注 rc 状态 | P2 | 仅文档 |
| ACT-08 | `build_b1.c` 溢出检查 | P3 | 安全加固 |
| ACT-09 | `build_dir.c` 溢出检查 | P3 | 安全加固 |
| ACT-10 | `b1_snapshot_t.weighted_avg` 清理 | P3 | 代码清理 |

---

## 3. 边界确认

### 3.1 明确在范围内的

1. ✅ 实现 `int32_search_find_range()` 完整功能（Path A + Path B1 两条路径）
2. ✅ 实现布隆过滤器（`#ifdef USE_BLOOM_FILTER` 编译开关）
3. ✅ Windows 平台 `platform_thread_yield()` 正确实现
4. ✅ meeting_011 ACT-03 ~ ACT-10（P2/P3 并行推进）
5. ✅ 新增测试覆盖：find_range 正确性 + bloom 假阳性率 + Windows 兼容

### 3.2 明确不在范围内的

1. ❌ SSE2 编译版本 — D-087 已降为 P3，本次 Phase 3 不实现（仅预留扩展点）
2. ❌ AVX-512 编译版本 — 同上
3. ❌ MSVC 编译器支持 — 本次仅 MinGW，MSVC 后续评估
4. ❌ Windows 下 AVX2 性能退化修复 — 已知风险，不阻塞交付（技术路线 §7）
5. ❌ `search_avx2_win.s` 清理 — 保留作为调查参考

### 3.3 待澄清的关键决策

以下问题需要在进入 Stage 2 前确认：

| # | 问题 | 背景 |
|---|------|------|
| Q1 | **布隆过滤器假阳性率目标**：建议 1%（m/n ≈ 9.6 bits/element），可接受吗？ | 总需求文档未指定 |
| Q2 | **布隆过滤器 Hash 函数数量**：建议 k=3（xxHash 三种 seed 变体），可接受吗？ | xxHash 已在仓库，支持任意 seed |
| Q3 | **find_range 在 B1 路径的实现策略**：B1 的 lo16 是无序的，范围查询需要特殊处理。建议在 B1 路径中 find_range 回退到 vals[] 标量二分（因 B1 lo16 不支持有序范围遍历），可接受吗？ | 技术债务 |
| Q4 | **Windows `platform_thread_yield()` 实现**：建议用 `_mm_pause()`（x86）+ `Sleep(0)`（通用）双策略，与 Linux `sched_yield()` 等价的 Windows API 是 `SwitchToThread()`。推荐方案：x86 用 `_mm_pause()`，否则 `Sleep(0)`。可接受吗？ | 技术路线 §7 标注了 COW Windows 困难 |

---

## 4. 现有系统的集成分析

### 4.1 find_range 集成

- **API 层**（api.c）：替换 stub → 调用内部 `lower_bound` + `upper_bound`
- **查询引擎层**：新增 `search_range.c`（或直接在 api.c 中实现，调用已有 `search_scalar.c` 函数）
- **Path A**：已排序 `vals[]` → 标量二分 lower_bound/upper_bound，简单直接
- **Path B1**：需决策——在 lo16 数组上无法高效 range 查询，建议回退 Path A 思路（即直接用 vals[] 二分）

### 4.2 布隆过滤器集成

- **平台层**：链接 xxHash（已有）
- **构建层**（api.c create）：`if (cfg->use_bloom)` → 分配 bloom 并批量插入
- **查询层**（api.c find）：`if (bloom)` → 先查 bloom，`NOT_PRESENT` 直接返回 `ERR_NOT_FOUND`
- **销毁层**（api.c destroy）：释放 bloom 内存
- **COW**（api.c rebuild）：新 bloom 随新数据一起构建

### 4.3 Windows 移植集成

- `platform_thread.h`：`platform_thread_yield()` 从空实现改为 `#ifdef _WIN32` + `_mm_pause()`/`Sleep(0)`
- 不改动其他文件
- 不需要 CMakeLists.txt 特殊处理（但 ACT-01 已完成 CMake 修复作为前提）

---

## 5. 风险识别

| 风险 | 等级 | 来源 |
|------|------|------|
| B1 路径范围查询语义不兼容 | 中 | B1 lo16 无序，需特殊回退策略 |
| 布隆过滤器内存占用（1% 假阳性 ≈ 12MB/10M） | 低 | 可控，编译开关 |
| MinGW AVX2 退化影响 find_range 性能 | 中 | 已知风险，仅影响 Windows 性能不阻塞正确性 |
| xxHash 集成引入额外依赖编译复杂度 | 低 | 仅 #ifdef 条件编译 |
| COW + Bloom 组合的并发正确性 | 低 | 复用现有 COW 框架，Bloom 作为快照一部分 |

---

## 6. 关联信息

- 总需求文档：[总需求文档.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/requirements/总需求文档.md)
- 技术路线文档：[技术路线.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/architecture/技术路线.md)
- Phase 2 审计决议：[03_decisions.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_011_phase2_audit_review/03_decisions.md)
- Phase 2 行动项：[04_action_items.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_011_phase2_audit_review/04_action_items.md)
