---
title: B1 内存池方案讨论会 — 消除指针税
meeting_id: meeting_008_b1_memory_pool
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Human
parent_doc: docs/meetings/meeting_index.md
parent_task: task_001_phase1_mvp
participants: [Architect, Algorithm, Backend, Fullstack, Security]
---

# B1 内存池方案讨论会 — 消除指针税

## 元信息

| 项目 | 值 |
|------|-----|
| 会议 ID | meeting_008_b1_memory_pool |
| 来源 | 人工指令：基于 meeting_007 POC 报告，讨论 B1 内存池优化方案 |
| 背景 | meeting_007 POC 三方向全未达标（Eytzinger 0.45x / B1 DIR bug / S-tree 1.21x）。B1 在绕过校验时展示出 3.45x-4.99x 潜力但数据不可靠。用户提出内存池方案消除 "指针税" |

## 决议摘要

5/5 全票通过。D-060~D-070：采纳单内存池方案（dir+lo16 合并到 pool，vals 独立）；内存池布局 dir[65537] + 28B padding + lo16[n]；dir 语义保持 lo16 元素偏移；switch(path) 取代 search_fn；3 项 P0 bug 修复先于内存池 POC；dir_validate 增强为 4 项检查；D-015 阈值暂不固定；COW 简化为无锁单指针。7 步行动计划。

## 核心议题

1. POC 报告中 B1 的瓶颈根因分析：DIR 构建 bug + AVX2 搜索逻辑 bug
2. 当前 B1 三指针架构（vals / lo16 / dir）的「指针税」问题
3. 用户提案：单内存池 + 偏移量直接计算，消除指针跳转
4. 内存布局方案设计：dir[65536] + lo16[] 全部在同一连续内存块
5. 性能与实现复杂度评估

## 文档状态看板

| 文档名 | 状态 | 最后更新 |
|--------|------|----------|
| 01_agenda.md | ✅ Frozen | 2026-05-30 |
| 02_discussion.md | ✅ Frozen | 2026-05-30 |
| 03_decisions.md | ✅ Frozen | 2026-05-30 |
| 04_action_items.md | ✅ Frozen | 2026-05-30 |
