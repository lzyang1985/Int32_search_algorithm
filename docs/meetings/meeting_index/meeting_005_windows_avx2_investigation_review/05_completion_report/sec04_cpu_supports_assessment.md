---
title: SEC-04 完成记录 — __builtin_cpu_supports 假阳性风险评估
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Architect
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
task_ref: SEC-04
resolution: DEEP-04 (FIXPLAN 第五波)
tags: [security, cpu-detection, false-positive, risk-assessment, SEC-04]
---

# SEC-04 `__builtin_cpu_supports` 假阳性 #UD 崩溃风险评估

**评估报告**：[ASSESSMENT_cpu_supports_false_positive_task_001_phase1_mvp.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/ASSESSMENT_cpu_supports_false_positive_task_001_phase1_mvp.md)

## 追踪链

```
S-TODO-03 (meeting_004) → ACT-15 → DEEP-04 (FIXPLAN) → SEC-04 (meeting_005)
```

## 问题

`platform_cpu_has_avx2()` 使用 `__builtin_cpu_supports("avx2")`。在非标准虚拟化/模拟环境（老版 QEMU/TCG、Wine 兼容层）中，CPUID 可能报告 AVX2=yes 但实际无法执行 AVX2 指令 → 首次查询 `vpbroadcastd` → #UD → SIGILL 崩溃。

## 评估结论

| 维度 | 评分 |
|------|------|
| 严重程度 | Medium（崩溃不可恢复） |
| 发生概率 | Low（仅非标准环境） |
| 综合风险 | **Low-Medium** |

## 推荐

维持当前方案，标记为 Known Limitation。Phase 2 前按需实施 Safe Probe（`sigsetjmp` + `vzeroupper` 探测指令）。
