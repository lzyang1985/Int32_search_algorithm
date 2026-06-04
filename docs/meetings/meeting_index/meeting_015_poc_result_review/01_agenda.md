---
title: 议程 — Int64 扩展 + Bloom 旁路 POC 结果评审会
meeting_id: meeting_015_poc_result_review
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 议程 — Int64 扩展 + Bloom 旁路 POC 结果评审会

## 会议信息

| 项目 | 内容 |
|------|------|
| 触发条件 | meeting_014 POC 执行完成，产出 [poc_int64_report.md](../../../decisions/poc_int64_report.md) |
| 评审对象 | POC 报告 + 三份 POC 源码（poc_int64_avx2.c / poc_int64_b1.c / poc_bloom_bypass.c） |
| 评审团队 | Architect, Algorithm, Backend, Fullstack, Security |
| 目标 | 评审 POC 结果，确认 Go/No-Go 决策，识别问题，明确后续行动 |

---

## 议程 1：POC 执行结果概览

| GATE | 验证项 | 结果 |
|------|--------|------|
| GATE-1 | Path A 正确性验证 | ✅ PASSED |
| GATE-2 | Path A 10M uniform AVX2 ≥ 1.2x Scalar | ❌ FAILED (0.58x) |
| GATE-3 | B1 10M uniform ≥ 1.5x Path A | ✅ PASSED (9.17x) |
| GATE-4 | Bloom Bypass 5 项验证 | ✅ PASSED |

---

## 议程 2：各专家独立评审

五位专家从各自专业角度对 POC 报告和代码进行了独立评审，评审意见汇总于 [02_discussion.md](02_discussion.md)。

---

## 议程 3：关键议题讨论

### 议题 A：GATE-2 阻塞性判定与整体 Go/No-Go 的矛盾

meeting_014 D-106 决策树中，GATE-2 被定义为「阻塞性，不通过 → int64 不可行」。但 GATE-3 超预期通过（9.17x），实际出现了一条决策树未覆盖的新路径。

### 议题 B：B1 回退阈值 (2000) 的来源与校准

POC 报告建议直接复用 int32 的 `B1_MAX_BUCKET_THRESHOLD = 2000`，但多位专家指出 int64 B1 的成本模型与 int32 完全不同，不能直接复用。

### 议题 C：Phase 1 交付物重新定义

meeting_012 D-095 规划的「Path A Only MVP」策略被 POC 结果完全推翻。Phase 1 需改为「Path B1 主线 + Scalar Fallback + Bloom Bypass」。

### 议题 D：POC 代码层面的问题汇总

包括 sign-flip 未抽取为独立函数、memory_order 不一致、dir int32_t 溢出、POC bloom 与生产 bloom 参数不对齐等。

### 议题 E：立项启动条件

确认是否可以在解决 Critical 问题前启动 Align 阶段，还是需要先解决所有 Critical 问题。

---

## 议程 4：决议与行动项

→ [03_decisions.md](03_decisions.md)
→ [04_action_items.md](04_action_items.md)
