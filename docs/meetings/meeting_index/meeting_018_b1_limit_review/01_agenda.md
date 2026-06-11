---
title: B1路径极限评审会 — 议程
meeting_id: meeting_018_b1_limit_review
status: Draft
created_at: 2026-06-08
updated_at: 2026-06-08
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_018_b1_limit_review/meeting_README.md
parent_task: root
---

# B1路径极限评审会 — 议程

## 现状基线

### bench_100.ps1 对比矩阵（人工提交的 bench_100.png）

| 场景 | V1 (旧版, us/q) | V3 (D-140修复后, us/q) | 变化 |
|------|----------------|----------------------|------|
| Int32 标准 ~50% hit (Bloom ON) | (见截图) | (见截图) | (见截图) |
| Int64 ~50% hit | (见截图) | (见截图) | (见截图) |
| Int32 Bloom OFF ~50% hit | (见截图) | (见截图) | (见截图) |
| Int32 Bloom OFF 100% hit | (见截图) | (见截图) | (见截图) |

**人工已给出的定性结论**：V3 最好成绩经常略慢，但更稳定；Int64 表现正面；认为已接近极限。

### V1 vs V3 代码差异摘要

| 差异 | V1 | V3 | 预期影响 |
|------|----|----|--------|
| D-140 2x SIMD 展开 | 原始单路16元素 | `#ifdef INT32_SEARCH_B1_UNROLL2` 包裹（默认关闭） | 无差异 |
| D-141 32B 对齐分配 | `malloc` | `platform_aligned_alloc(32)` | 构建期，热路径无影响 |
| D-142 小桶标量路径 | 所有桶走 SIMD | 桶 < 8 → 标量扫描 | 预期省 ~2cy 均摊 |
| D-143 防御检查 | 仅 `start >= end` | 新增 `start<0` / `end<0` / `(size_t)end>n` | +1-2 条件判断 |

### Int64 B1 路径现状

```c
// search_b1_int64.c — 4路 _mm256_cmpeq_epi64 扫描
// 无 D-140~D-143 等同优化（未实现小桶标量路径、未实现2x展开）
```

## 议题

### 议题1: benchmark 结果专家解读
- 为什么 V3 最好成绩略慢？是预期内的代价还是意外退化？
- D-142 小桶路径的收益是否被 D-143 防御检查抵消？
- "更稳定" 的来源是什么？

### 议题2: Int64 B1 路径评估
- Int64 的 4 路 cmpeq_epi64 在 B1 路径上有无改进空间？
- Int32 的 D-142/D-143 等微优化是否值得移植到 Int64？

### 议题3: B1 路径剩余空间终判
- D-140 条件编译在正确编译选项下（-fno-unroll-loops）是否值得花时间重新验证？
- 还有没有 meeting_017 未覆盖的 B1 微优化空间？
- 内存子系统瓶颈 vs 计算瓶颈的再评估

### 议题4: 项目终局建议
- 当前是否已达到"定向 P1 优化"的合理终点？
- D-130~D-133 四个 POC 优先级是否需要调整？
- 是否应该进入维护模式？
