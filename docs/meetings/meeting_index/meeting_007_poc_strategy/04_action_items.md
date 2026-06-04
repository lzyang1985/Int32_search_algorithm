---
title: 行动项 — POC 策略讨论
meeting_id: meeting_007_poc_strategy
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_007_poc_strategy/meeting_README.md
parent_task: task_001_phase1_mvp
---

# 行动项 — POC 策略讨论会

---

## 阶段 1：DEEP-05 定量分析（0.5 天）

| 编号 | 行动 | 执行人 | 关联决议 | 预计耗时 |
|:---:|------|--------|:---:|:---:|
| POC-01 | 在 Linux 上 `perf stat` 采样 Phase 1 `search_avx2_find`：分支预测失败率、vmovmskps 发射数、cache miss | Agent_Algorithm | D-053 | 1h |
| POC-02 | 对照分析 POC v3 旧版 `cmpgt(key,vec)` 的相同指标 | Agent_Algorithm | D-053 | 0.5h |
| POC-03 | 从 perf 数据自洽解释 ~17x 退化（168→2852 cy/q），输出瓶颈 cycle 占比表 | Agent_Algorithm | D-053 | 0.5h |

---

## 阶段 2：Eytzinger POC（1 天）

| 编号 | 行动 | 执行人 | 关联决议 | 预计耗时 |
|:---:|------|--------|:---:|:---:|
| POC-04 | 编写 `src/poc_eytzinger.c`：BFS 布局重排 + branchless 搜索 | Agent_Executor | D-053 | 3h |
| POC-05 | Benchmark：1M/10M uniform，vs Phase 1 标量二分 | Agent_Executor | D-056 | 1h |
| POC-06 | 边界数据集 + ASan/UBSan 验证 | Agent_Executor | D-057 | 1h |
| POC-07 | 输出 Eytzinger vs 标量加速比 + bit/cy 标注 | Agent_Algorithm | D-058 | 0.5h |

---

## 阶段 3：Path B1 重验证（0.5 天）

| 编号 | 行动 | 执行人 | 关联决议 | 预计耗时 |
|:---:|------|--------|:---:|:---:|
| POC-08 | 修正 `poc_benchmark_v3.c` 计时为 `__rdtscp() + _mm_lfence()` | Agent_Executor | D-053 | 15min |
| POC-09 | Benchmark：1.5M/10M uniform + 5M skewed，vs Phase 1 标量二分 | Agent_Executor | D-056 | 30min |
| POC-10 | 输出 B1 加速比 + crossover 验证 | Agent_Algorithm | D-058 | 15min |

---

## 阶段 4：S-tree POC（条件触发，~1 天）

| 编号 | 行动 | 执行人 | 关联决议 | 触发条件 |
|:---:|------|--------|:---:|:---:|
| POC-11 | 编写 `src/poc_stree.c`：16-key B-tree + SIMD 节点查找 | Agent_Executor | D-053 | Eytzinger < 2x |
| POC-12 | Benchmark + 边界验证 + ASan/UBSan | Agent_Executor | D-056 | Eytzinger < 2x |
| POC-13 | 输出 S-tree 加速比 | Agent_Algorithm | D-058 | Eytzinger < 2x |

---

## 前置依赖：Phase 1 P0 收尾（~1.5h）

| 编号 | 行动 | 关联决议 | 预计耗时 |
|:---:|------|:---:|:---:|
| PRE-01 | 执行 meeting_006 ACT-01~14 P0 项（文档 + 代码修复） | D-059 | 1.5h |

---

## 工作量汇总

| 阶段 | 项目数 | 总耗时 | 触发条件 |
|------|:---:|:---:|------|
| PRE | 14 | 1.5h | 无条件 |
| 阶段 1 (DEEP-05) | 3 | 0.5d | 无条件 |
| 阶段 2 (Eytzinger) | 4 | 1d | 无条件 |
| 阶段 3 (B1) | 3 | 0.5d | 无条件 |
| 阶段 4 (S-tree) | 3 | ~1d | Eytzinger < 2x |
| **最大合计** | **27** | **~3.5d + 1.5h** | — |
| **最小区间（不含 S-tree）** | **24** | **~2.5d + 1.5h** | — |

---

> **所有行动项已分配。**
> 前置条件：meeting_006 P0 收尾 → meeting_006 Frozen → POC 启动。
> S-tree 为条件触发，不阻塞前三阶段。
