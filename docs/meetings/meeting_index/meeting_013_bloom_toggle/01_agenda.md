---
title: 议程 — 布隆过滤器开关特性讨论
meeting_id: meeting_013_bloom_toggle
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 议程 — 布隆过滤器开关特性讨论会议

## 1. 议题背景

当前实现中，布隆过滤器的启用由两部分控制：
1. **编译期**：`-DINT32_SEARCH_USE_BLOOM` 宏控制是否编译 bloom 相关代码
2. **构建期**：`cfg.use_bloom = 1` 决定 `int32_search_create()` 时是否构建 bloom
3. **查询期**：一旦 bloom 存在，所有 `int32_search_find()` 都先走 `bloom_query()` 预筛

**问题**：对于"确定 key 一定存在于集合中"的查找场景，布隆过滤器的预筛步骤是纯开销——此时 bloom_query() 总是返回 1（必定通过），但仍需计算 3 次 XXH32 哈希 + 3 次位检查。

## 2. 讨论议题

### 议题 1：是否提供查询级别的 Bloom 旁路能力

是否应该允许用户在某些查询调用时跳过布隆过滤器？

**潜在形态**：
- A) 新增 `int32_search_find_no_bloom()` 函数（绕过 bloom）
- B) `find()` 函数增加 `flags` 参数（如 `INT32_SEARCH_FLAG_SKIP_BLOOM`）
- C) cfg 增加 `skip_bloom` 字段，在 find 调用间可切换
- D) 不提供此能力（维持现状）

### 议题 2：对"确定存在"场景的性能影响

- 布隆过滤器对确定存在的 key 的额外开销有多大？
- 在 Path A 和 Path B1 下，bloom 预筛的 latency 占比分别是多少？
- 是否存在某些数据规模/分布下，bloom 预筛反而拖慢确定存在的查询？

### 议题 3：运行时 Bloom 开关的设计空间

如果决定支持，具体设计形态：
- API 层面的接口形态（哪个方案最符合现有设计哲学）
- 与 COW 并发模型的交互（重建时 bloom 如何切换？）
- 与 Path A / Path B1 的交互
- 向后兼容性（已有 cfg.use_bloom 语义不变）

### 议题 4：对 int64 扩展的影响

meeting_012 已确认 int64 扩展可行，Bloom 开关特性在 int64 库中是否应同步支持？

## 3. 预期产出

1. 决议：是否提供 Bloom 旁路能力（Go/No-Go）
2. 决议：如果提供，采用的 API 形态
3. 决议：对现有 API 的兼容性策略
4. 决议：int64 扩展中的同步策略
5. 行动项列表（优先级排序）
