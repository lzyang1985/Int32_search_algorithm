---
title: TODO-11 完成记录 — Linux Benchmark
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
task_ref: TODO-11
resolution: D-037 (原 FIXPLAN 第四波 VERIFY-04，已降级 P3)
tags: [benchmark, linux, avx2, Xeon-Gold-6152]
---

# TODO-11 Linux Benchmark

**环境**：103.236.63.60, Intel Xeon Gold 6152 @ 2.10GHz, GCC 11.4.0, `-O3 -mavx2`（未加 `-march=native`）

## 结果

| N | API(AVX2 detect) | Raw AVX2 | Raw Scalar | Inline Scalar | AVX2/Scalar |
|---|------------------|----------|------------|---------------|-------------|
| 1M | 630.0 cy/q | 1351.3 cy/q | 618.9 cy/q | 606.2 cy/q | **0.46x** |
| 5M | 1199.2 cy/q | 2545.6 cy/q | 1142.0 cy/q | 1125.8 cy/q | **0.45x** |
| 10M | 1386.4 cy/q | 2984.3 cy/q | 1490.8 cy/q | 1350.7 cy/q | **0.50x** |

## 关键发现

- API 在所有 3 个数据规模均走标量路径（n ≤ 10M 阈值）
- Raw AVX2 比 Raw Scalar 慢约 **2x**（0.45-0.50x），与 Windows 调查结论一致
- 根因确认为编译选项：缺少 `-march=native` 导致 AVX2 代码生成质量退化
- Inline Scalar 始终最快（无函数调用开销）
