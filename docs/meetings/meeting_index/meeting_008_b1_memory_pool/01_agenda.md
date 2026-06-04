---
title: 会议议程 — B1 内存池方案讨论会
meeting_id: meeting_008_b1_memory_pool
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Human
parent_doc: docs/meetings/meeting_index/meeting_008_b1_memory_pool/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_007_poc_strategy/05_poc_execution_report.md
  - docs/meetings/meeting_index/meeting_007_poc_strategy/03_decisions.md
  - docs/architecture/技术路线.md
  - docs/requirements/总需求文档.md
  - src/poc_benchmark_v3.c
tags: [b1, memory-pool, phase2, optimization]
---

# 会议议程 — B1 内存池方案讨论会

## 1. 上期待办回顾（meeting_007 D-058/059）

- meeting_007 POC 三方向全未达标，触发 RFC
- DEEP-05: AVX2 结构性瓶颈确认
- Eytzinger: 0.45x ❌
- B1: DIR 校验 bug + AVX2 搜索逻辑 bug，数据不可靠 ⚠️
- S-tree: 1.21x < 1.5x ❌

## 2. 议题一：B1 当前瓶颈根因分析

### 2.1 DIR 构建 bug

`high16_dir_build` 的后向填充逻辑在 dir[i]==-1 且 dir[i+1]==n 时出现不一致，导致 `dir[65536] != n` 校验失败。

### 2.2 AVX2 搜索逻辑 bug

POC v3 中 `search_avx2_binary` 使用 `cmpgt(key,vec)` + `le_count = popcount(mask ^ 0xFF)`，DEEP-05 已证实 hits=0（搜索逻辑错误）。这污染了 POC v3 中所有 AVX2(A) 的基准数据，也连带质疑 B1 数据可信度。

### 2.3 三指针「指针税」

当前 B1 查询路径需要三次指针解引用：
- `dir[h]` → 定位高 16 位桶起始
- `lo16[start+i]` → 扫描低 16 位数组
- `vals[pos]` → 回验全值

每次指针访问触发 CPU 地址解析流水线停顿，这是「指针税」的核心。

## 3. 议题二：用户提案 — 单内存池 + 偏移量直接计算

### 3.1 核心思路

- 一次性申请足够大的单块连续内存
- high16 dir + lo16 数组全部放入同一内存池
- 查询时用偏移量直接计算位置，**无需指针跳转**

### 3.2 内存布局示意

```
┌──────────────────────────────────────────────────────┐
│                单内存池 (连续地址空间)                    │
├───────────────┬──────────────────────────────────────┤
│ dir[65536+1]  │  lo16 数组 (按桶排列，非稀疏)          │
│ (int32_t)     │  (uint16_t)                           │
│ 262 KB        │  2n B (~20 MB @ 10M)                  │
├───────────────┴──────────────────────────────────────┤
│ dir[i] 存储的是 lo16 数组中桶 i 的起始偏移（非指针）    │
│ 查询: h = key>>16 → offset = dir[h] → 扫描 lo16[offset:]│
└──────────────────────────────────────────────────────┘
```

### 3.3 与传统 B1 的关键区别

| 项目 | 传统 B1 | 内存池 B1 |
|------|---------|-----------|
| 内存布局 | 3 块独立分配（malloc ×3） | 1 块连续分配（malloc ×1） |
| dir 存储 | 指向 vals 的索引 | 指向 lo16 数组的偏移 |
| lo16 访问 | `lo16[start+i]`（指针+偏移） | `pool[dir_offset + i]`（纯偏移） |
| 全值回验 | `vals[pos]`（第二次指针跳） | 可直接存回验索引或映射 |
| 构建复杂度 | 独立分配 + 一致性校验 | 一次分配 + 偏移映射 |
| 内存局部性 | 差（三块分散） | 优（一块连续） |

## 4. 各 Agent 讨论阵地

| Agent | 讨论焦点 |
|-------|----------|
| Architect | 内存池方案与现有四层架构的集成、API 影响、COW 兼容性 |
| Algorithm | 查询性能分析：偏移计算 vs 指针解引用的 CPU 周期差异；lo16 扫描是否有 SIMD 加速空间 |
| Backend | 内存池实现细节：对齐要求、偏移映射、构建/销毁逻辑 |
| Fullstack | API 层面影响：`int32_search_t` 不透明句柄是否受影响 |
| Security | 内存安全：溢出检测、边界校验、SIMD 非对齐读取安全 |
