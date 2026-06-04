---
title: TODO-01 编译选项对比实验报告
status: Frozen
created_at: 2026-05-29
updated_at: 2026-05-30
author: Agent_Executor
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
task_ref: TODO-01
resolution: D-037
tags: [benchmark, avx2, compiler-options, E1-E4, windows, linux]
---

# TODO-01 编译选项对比实验报告

## 1. 测试环境

### Windows

| 项目 | 值 |
|------|-----|
| CPU | Intel Xeon Processor (Skylake, IBRS) |
| OS | Windows (MinGW-w64) |
| 编译器 | GCC 15.2.0 |
| 计时方法 | `__rdtscp()` + `_mm_lfence()` |
| 预热策略 | ~500ms 循环预热 |
| 查询规模 | 1M 查询 / 50% 命中率 / 均匀随机 |
| 随机种子 | `INT32SEARCH_BENCH_SEED=42` |

### Linux（服务器 103.236.63.60）

| 项目 | 值 |
|------|-----|
| CPU | Intel Xeon Gold 6152 @ 2.10GHz (16 cores, AVX-512) |
| OS | Ubuntu 22.04 LTS (Kernel 5.15.0-30) |
| 编译器 | GCC 11.4.0 |

---

## 2. 实验矩阵与结果

### E1：基线复现 (`-O3 -mavx2`)

| 平台 | N=1M AVX2 | N=1M Scalar | AVX2/Scalar |
|------|-----------|-------------|-------------|
| Windows (GCC 15.2) | 916.7 cy/q | — | **0.48x** |
| Linux (GCC 11.4) | 1308.8 cy/q | — | **0.44x** |

✅ 基线复现成功。

### E2：`-march=native`

| 平台 | AVX2/Scalar | vs E1 |
|------|-------------|-------|
| Windows | 0.45x | < 3% 波动 |
| Linux | 0.48x | < 5% 波动 |

❌ `-march=native` 未恢复性能。

### E3：汇编对比

热循环指令 `vmovdqu/vpcmpgtd/vmovmskps/popcnt` 在 E1/E2 间**完全相同**（0 字节差异）。

### E4：POC 代码公平对比

原 POC "127 cy/q (3.53x)" 证实为 **bug 假象**（56.3% 错误率）。修复版 POC 仍慢于标量（0.67-0.84x）。

---

## 3. 最终结论

> **根因是算法层面的效率瓶颈。** AVX2 SIMD 辅助二分搜索每次迭代开销 (~12 cy) 超过迭代次数减少的收益。N=1M-10M 数据规模下，标量二分搜索是最优选择。

---

## 4. 重复测量统计（N=5，TODO-07 产物）

| Group | N=1M | N=5M | N=10M |
|-------|------|------|-------|
| Raw AVX2 | 823.7 ± 27.5 | 1247.7 ± 51.4 | 1429.3 ± 19.4 |
| Raw Scalar | 401.8 ± 9.3 | 666.8 ± 26.6 | 851.6 ± 86.0 |
| Inline Scalar | 392.9 ± 14.1 | 635.7 ± 27.9 | 799.7 ± 18.7 |

- AVX2 σ/μ ≈ 1.4%-4.1%（稳定）
- 5 轮无一例外 AVX2 均慢于标量
- API 函数指针开销可忽略（API vs Inline ≈ 0.94-0.98x）
