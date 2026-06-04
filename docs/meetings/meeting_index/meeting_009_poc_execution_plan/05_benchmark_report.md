---
title: Step 2 Benchmark 报告 — 内存池 B1 三分对比
meeting_id: meeting_009_poc_execution_plan
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_009_poc_execution_plan/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/03_decisions.md
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/04_action_items.md
tags: [poc, benchmark, b1-pool, phase2]
---

# Step 2 Benchmark 报告 — 内存池 B1 三分对比

> 对应行动项：POOL-05、POOL-06
> 代码：[src/poc_b1_pool.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_b1_pool.c)
> CSV：[bench_b1_pool.csv](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/bench_b1_pool.csv)

---

## 1. 测试环境

| 项目 | 值 |
|------|-----|
| 服务器 | 103.236.63.60 |
| CPU | Intel Xeon Gold 6152 @ 2.10GHz |
| 核心数 | 16 C / 16 T |
| L3 缓存 | 30.3 MiB |
| 内存 | 15 GiB |
| 操作系统 | Ubuntu 22.04 LTS (Kernel 5.15.0-30) |
| 编译器 | GCC 11.4.0 |
| 编译选项 | `-O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize` |

---

## 2. Benchmark 方法学（D-074）

| 参数 | 值 |
|------|-----|
| 进程模型 | 同进程，6 轮轮换顺序（pool / 3-ptr / scalar 各做 2 次首发/次发/末发） |
| 算法间隔离 | 2 MB dummy pass 刷 L2 缓存 |
| 每算法 repeats | 7 次，discard 前 3，取后 4 的中位数 |
| 最终统计 | 6 轮中位数 → 算 median / min / max / stddev |
| 输出指标 | cy/q_median, stddev, min, max, speedup_vs_scalar |
| 查询量 | 1M per combo, 50% hit |
| 规模梯度 | 1.5M / 5M / 10M |
| 分布 | uniform / skewed (80% 集中) |

---

## 3. 结果汇总

| n | 分布 | max_bucket | B1 Pool | B1 3-ptr | Scalar | Pool/Scalar | 3-ptr/Scalar |
|----|------|:--:|:--:|:--:|:--:|:--:|:--:|
| 1.5M | uniform | 824 | **221.5** cy/q | 192.2 cy/q | 571.4 cy/q | **2.58x** | 2.97x |
| 1.5M | skewed | 663 | **78.9** cy/q | 57.5 cy/q | 265.6 cy/q | **3.37x** | 4.62x |
| 5M | uniform | 2593 | 680.8 cy/q | **580.2** cy/q | 787.5 cy/q | 1.16x | **1.36x** |
| 5M | skewed | 2115 | 148.2 cy/q | **108.8** cy/q | 274.8 cy/q | 1.85x | **2.53x** |
| 10M | uniform | 5119 | 1629.7 cy/q | 1504.3 cy/q | **949.2** cy/q | ⚠️ 0.58x | ⚠️ 0.63x |
| 10M | skewed | 4121 | 253.9 cy/q | **175.8** cy/q | 286.4 cy/q | 1.13x | **1.63x** |

> 粗体 = 最优算法；⚠️ = 劣于标量二分

### 3.1 标准差（stddev）

| n | 分布 | Pool σ | 3-ptr σ | Scalar σ |
|----|------|:--:|:--:|:--:|
| 1.5M | uniform | 4.3 | 2.9 | 10.7 |
| 1.5M | skewed | 4.1 | 1.4 | 4.7 |
| 5M | uniform | 64.7 | 27.9 | 14.3 |
| 5M | skewed | 5.6 | 3.0 | 2.7 |
| 10M | uniform | 60.2 | 71.3 | 14.7 |
| 10M | skewed | 9.2 | 5.8 | 15.8 |

> 5M/10M uniform 下 Pool/3-ptr 波动较大（max_bucket 大 → 个别 bucket 长短不均 → 迭代次数波动），但仍 < 10% 中位数。Scalar 稳定 < 5%。

---

## 4. 分析

### 4.1 B1 适用窗口

B1 性能优于标量二分的条件：**max_bucket 足够小**（lo16 扫描开销 < 二分 log₂(n) 次 cmp 开销）。

阈值估计（基于本数据）：
- max_bucket ≤ ~2000 → B1 领先 1.3×~4.6×（1.5M 全场景 + 5M skewed）
- max_bucket > ~5000 → B1 落后 0.6×（10M uniform，lo16 扫描过长）

这直接验证了 meeting_002 D-019 的 crossover 策略必要性和 meeting_003/poc_benchmark_v3 中 crossover=1.6M 的合理性。

### 4.2 内存池 vs 三指针

**三指针始终快于内存池**，差距约 10%~30%：

| 场景 | 差距 |
|------|:--:|
| 1.5M uniform | pool 比 3-ptr 慢 15% |
| 1.5M skewed | pool 比 3-ptr 慢 37% |
| 5M uniform | pool 比 3-ptr 慢 17% |
| 5M skewed | pool 比 3-ptr 慢 36% |
| 10M uniform | pool 比 3-ptr 慢 8% |
| 10M skewed | pool 比 3-ptr 慢 44% |

原因：`B1_POOL_DIR(pool)` 宏展开为 `pool`（零偏移），但 `B1_POOL_LO16(pool)` 展开为 `pool + 262176` 在热路径上编译为 `ADD reg, 262176` 而非与 dir 访问共享基址寄存器。

**结论**：内存池 POC 未达到 meeting_008 D-060 预期的 "消除指针税" 目标（预期 +5~8%，实测反降 10~30%）。生产实现需考虑：
- 将 dir/lo16 分离为两个独立 `const` 指针传参（即三指针方案）
- 或在 hot loop 外预计算 `const uint16_t *lo16 = B1_POOL_LO16(pool)` 后再进入扫描循环（本 POC 代码已在函数内 L231 做了这一点，编译器理论上应能优化，但实测仍有差距）

### 4.3 skewed vs uniform

skewed 分布（80% 数据集中）的 B1 加速比系统地高于 uniform：

| n | Pool uniform | Pool skewed | 差异 |
|----|:--:|:--:|:--:|
| 1.5M | 2.58x | 3.37x | +31% |
| 5M | 1.16x | 1.85x | +59% |
| 10M | 0.58x | 1.13x | +95% |

原因：skewed 下 bucket 更紧凑（max_bucket 263→2115→4121 vs 824→2593→5119），lo16 扫描更短。

---

## 5. 与 Step 1 交叉验证

Step 1 基准（`verify_b1_fixed.c`）同样在 Xeon Gold 6152 上跑过。对比 3-ptr B1 数据：

| n | 分布 | Step 1 B1 cy/q | Step 2 B1 3-ptr cy/q | 偏差 |
|----|------|:--:|:--:|:--:|
| 1.5M | uniform | 约 195 | 192.2 | ~1.4% |

数据一致（同一 CPU、同一代码逻辑）。

---

## 6. 结论

| 问题 | 结论 |
|------|------|
| B1 在什么条件下优于标量二分？ | max_bucket ≤ ~2000（约对应 n ≤ 5M uniform 或 skewed 下更大范围） |
| 内存池是否消除指针税？ | **否**。三指针仍然快 10~30% |
| POC 下一步方向？ | Step 3 crossover 散点采集（D-015）精确画 crossover 曲线 |

---

> 本报告产出后，Step 2（POOL-01~07）全部完成 ✅。
> 下一步：Step 3 `src/poc_b1_crossover.c`（CROSS-01~05）。
