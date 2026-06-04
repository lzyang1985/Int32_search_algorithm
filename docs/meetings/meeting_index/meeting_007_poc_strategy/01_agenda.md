---
title: 议程 — POC 策略讨论
meeting_id: meeting_007_poc_strategy
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_007_poc_strategy/meeting_README.md
parent_task: task_001_phase1_mvp
---

# 议程 — POC 策略讨论会

---

## 一、背景

### 已知事实
- Path A AVX2 SIMD 二分：信息效率 0.250 bit/cy，不如标量 0.286 bit/cy。**方向已终止**。
- meeting_006 D-046 设定了 Phase 2 候选方向：
  - P0: Path B1（lo16 跳过 + 构建时分桶）
  - P0: Eytzinger layout + branchless binary search
  - P1: S-tree（16-key B-tree + SIMD 节点查找）
- DEEP-05（POC v3 vs Phase 1 差异分析）已规划，meeting_006 ACT-17 待执行
- 算法工程师在 meeting_006 建议 Phase 2 前做 Path B1 微型 POC

### 当前不确定性
1. Path B1 的 lo16 + dir 结构在实际 benchmark 中能达到什么加速比？
2. Eytzinger layout 在有序 int32 数组上是否优于标准二分？
3. S-tree 16-key SIMD 节点查找的理论优势能否在 2026 年的硬件上兑现？
4. 三个方向之间是否存在排他关系，还是可以组合？

---

## 二、待讨论议题

### 议题 1：POC 的优先级排序

Phase 2 有三个候选方向。有限的 POC 时间（建议 ≤1 天/方向）应该优先投入哪个？

- **方案 A**：Path B1 微型 POC — 风险最低（已有完整设计），直接验证 lo16 跳过是否能绕过 AVX2 二分瓶颈
- **方案 B**：Eytzinger POC — 业界已知最优布局，但需要验证在 int32 + AVX2 场景下的实际收益
- **方案 C**：S-tree POC — 理论最优（0.333 bit/cy），但实现复杂度最高
- **方案 D**：先跑 DEEP-05（POC v3 vs Phase 1 定量差异），用数据而非直觉选方向

### 议题 2：每个候选方向的 POC 设计

对每个方向，需要明确：
- 最小可行代码量（多少行 C 代码）
- 验收标准（加速比阈值、正确性标准）
- 预计耗时
- 失败标准（什么情况下放弃该方向）

### 议题 3：POC 对 Phase 2 go/no-go 的影响

- 如果 Path B1 POC 也达不到 1.5x 加速比，是否重新评估整个"有序 int32 数组的 SIMD 加速"这一命题？
- 如果 Eytzinger POC > 2x，是否直接跳过 B1 进入 Eytzinger + S-tree？

### 议题 4：POC 执行策略

- 单线程顺序执行（一个方向验证完再下一个）还是并行？
- 是否需要先跑 DEEP-05 为 POC 设计提供量化依据？
- POC 代码是否需要在当前项目中落地（可以被 Phase 2 复用），还是独立于项目？
