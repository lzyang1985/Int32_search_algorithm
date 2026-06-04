---
title: TODO-07 完成记录 — Benchmark N=5 重复测量 ±σ
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
task_ref: TODO-07
resolution: A-01
tags: [benchmark, statistics, stddev, A-01]
---

# TODO-07 Benchmark N=5 重复测量 ±σ

数据已集成至 [todo01_benchmark_options.md](todo01_benchmark_options.md) §4。

**采集方法**：`INT32SEARCH_BENCH_SEED=42`，5 次独立运行。

## 均值 ± 标准差

| Group | N=1M | N=5M | N=10M |
|-------|------|------|-------|
| Raw AVX2 | 823.7 ± 27.5 | 1247.7 ± 51.4 | 1429.3 ± 19.4 |
| Raw Scalar | 401.8 ± 9.3 | 666.8 ± 26.6 | 851.6 ± 86.0 |
| Inline Scalar | 392.9 ± 14.1 | 635.7 ± 27.9 | 799.7 ± 18.7 |

## 关键发现

- AVX2 σ/μ ≈ 1.4%-4.1%（稳定）
- 5 轮无一例外 AVX2 均慢于标量
- API 函数指针方案开销可忽略（API VCX2 vs Inline Scalar ≈ 0.94-0.98x）
