---
title: 会议议程 — Int64 扩展可行性研讨
meeting_id: meeting_012_int64_feasibility
status: Draft
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 会议议程 — Int64 扩展可行性研讨会议

## 1. 会议目标

仅讨论本项目二期工程是否具备从 **Int32 → Int64** 扩展的**技术可行性**，不做具体实现方案设计。

## 2. 核心议题

### 议题 1：Path A — AVX2 8 路 SIMD 二分 → Int64 迁移可行性
- 当前 Path A 使用 `_mm256_cmpgt_epi32` 同时对 8 个 int32 比较
- 迁移到 int64 后：`_mm256_cmpgt_epi64` 仅支持 4 路并行（256-bit / 64-bit = 4）
- 关键变更：block 对齐从 `& ~7` → `& ~3`，while 条件从 `>= 8` → `>= 4`
- 性能预期：IPC 减半（8路→4路），但标量二分操作数翻倍（比较64位）

### 议题 2：Path B1 — high16 目录 + lo16 扫描 → Int64 迁移可行性
- 当前 B1 核心设计：high16 作为目录索引 → `dir[65536]`（2^16 = 65K 条目）
- Int64 场景下，"高位"是 48 bits，无法直接用 16-bit 索引
- 可选方案讨论：
  - 方案 a：高32位目录 → `dir[2^32]` ≈ 4G 条目，不可行
  - 方案 b：高 20/24 位 + 分层目录 → 内存增量巨大
  - 方案 c：放弃 B1，仅保留 Path A → 损失密集型小桶场景的加速优势
  - 方案 d：哈希桶替代高16目录 → 引入哈希冲突，不再保证 O(1) 定位

### 议题 3：Bloom Filter / Scalar / API / 内存
- Bloom Filter：xxHash 天然支持变长输入，迁移 trivial
- Scalar Search：二分逻辑与位宽无关，迁移 trivial
- API 变更：`int32_t` → `int64_t` 全量替换，是否为独立库还是共存？
- 内存占用：vals 从 4 bytes/elem → 8 bytes/elem（10M ≈ 80MB）

### 议题 4：AVX-512 视角
- AVX-512 下 `_mm512_cmpgt_epi64` 支持 8 路 int64 并行
- 若考虑 AVX-512 优先，可恢复与当前 int32+AVX2 同等的并行度
- 但 AVX-512 需要 CPU 支持，受众面窄

## 3. 判定标准

| 结论 | 定义 |
|------|------|
| ✅ 可行 | 所有核心路径存在明确的、合理的迁移路径 |
| ⚠️ 有条件可行 | 部分路径存在重大困难，但存在替代方案 |
| ❌ 不可行 | 核心算法存在根本性障碍，无法解决 |

## 4. 讨论规则
- 本轮仅讨论"是否存在可行性"，不涉及实现方案设计
- 各 Agent 依次发言，交叉讨论不超过 5 轮
- 最终由 Architect-Project-Manager 汇总结论
