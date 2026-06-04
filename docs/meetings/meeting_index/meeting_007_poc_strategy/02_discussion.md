---
title: 讨论记录 — POC 策略讨论
meeting_id: meeting_007_poc_strategy
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_007_poc_strategy/meeting_README.md
parent_task: task_001_phase1_mvp
---

# 讨论记录 — POC 策略讨论会

---

## 议题 1：POC 优先级排序

### 算法工程师

> 四个候选方向的信息/努力比：
>
> | 优先级 | 方向 | 信息/努力比 | 理由 |
> |:---:|------|:---:|------|
> | P0 | DEEP-05 | ★★★★★ | 零代码，回答"瓶颈在哪"，指导后续所有方向 |
> | P0 | Eytzinger | ★★★★ | 50行，回答"布局能否赢标量"，是 Phase 2 go/no-go 问题 |
> | P0 | Path B1 | ★★★ | 80行，验证已知结论 |
> | P1 | S-tree | ★★ | 200行，理论最优但实现风险最高 |
>
> Eytzinger 是最纯粹的问题：数据布局能否打败分支预测？它不碰 SIMD，整个 Phase 2 如果连纯标量 branchless 都赢不了标量二分，那 S-tree 更难赢（因为 S-tree 节点内仍用 `vmovmskps`）。

### 架构项目经理

> 同意算法工程师的优先级排序。补充三点：
> 1. DEEP-05 必须先跑——它告诉我们 Path A 正确版本的瓶颈分布，直接决定 Eytzinger 的验收阈值是否合理
> 2. B1 已有完整 POC 代码（`poc_benchmark_v3.c`），只需重新编译运行，实际耗时 < 1 小时
> 3. S-tree 应设为条件触发——仅当前两个方向均有正收益时才值得投入

### 后端工程师

> 从代码量角度：Eytzinger 最少（纯标量、零额外数据结构）、B1 已有代码直接可跑、S-tree 最复杂（padding + interleaved layout + child pointer 数组）。工程可行性排序与算法优先级一致。

### 代码安全专家

> 三个方向都引入了新的内存安全风险，但没有方向性的安全偏好。关键是 POC 阶段必须跑 ASan/UBSan，不能推迟到 Phase 2。

---

## 议题 2：各方向的 POC 设计

### Eytzinger POC（最高优先级新增方向）

| 维度 | 设计 |
|------|------|
| 代码量 | ~50 行核心 + ~200 行基础设施 = ~250 行总规模 |
| 核心算法 | BFS 重排已排序数组 → `while(i < n) i = 2*i + 1 + (key > tree[i])` + CMOV 消分支 |
| 验收标准 | 10M 均匀 50% 命中，Eytzinger vs Phase 1 标量二分，≥ 2.0x 即通过 |
| 失败标准 | 比标量二分慢（即布局收益不超过分支预测正确率优势） |
| 构建开销 | O(n)，10M 元素 ~2-5ms。查询 10M 次可轻松回收 |
| 安全关注 | 递归栈深 O(log n) 安全；1-based→0-based 索引转换需边界数据集验证 |

### Path B1 POC（已有代码，重新验证）

| 维度 | 设计 |
|------|------|
| 代码 | `poc_benchmark_v3.c` 已有 `search_b1_high16_lo16` (~32行) + `high16_dir_build` (~15行) |
| 验收标准 | 1.5M/10M uniform + 5M skewed。复现 crossover ~1.6M。≥1.5x vs 标量 @ 1.5M 即进入 Phase 2 |
| 10M 风险 | 架构项目经理指出：B1 在 10M 均匀下 bucket_size ~1000 >> 150 阈值，必回退 Path A。而 Path A 已被实质禁用。B1 是"小数据量特化解" |
| 安全关注 | 代码安全专家：dir[65536] 是 Critical 级风险，必须读写时边界验证 |
| 计时精度 | 后端工程师：`poc_benchmark_v3.c` 仍用 `__rdtsc()` 无 lfence，建议改 `__rdtscp() + _mm_lfence()` |

### S-tree POC（条件触发）

| 维度 | 设计 |
|------|------|
| 代码量 | ~110 行核心 + ~200 行基础设施 = ~310 行 |
| 理论优势 | 0.333 bit/cy，log₁₆ n 步 |
| 主要风险 | 仍用 `vmovmskps`（Path A 瓶颈#1），节点不满 16 keys 时掩码垃圾位 |
| 验收标准 | ≥1.5x vs 标量 @ 10M |
| 触发条件 | Eytzinger < 2x 时才启动（用 S-tree 做后备） |

### DEEP-05（先跑，信息密度最高）

| 维度 | 设计 |
|------|------|
| 代码量 | 0 行新代码 |
| 方法 | `perf stat` 采样 Phase 1 正确 AVX2 的分支预测失败率、vmovmskps 发射数、cache miss |
| 产出 | 一张表：三大瓶颈的 cycle 占比 |
| 耗时 | 0.5 天 |

---

## 议题 3：go/no-go 决策树

### 架构项目经理的决策树设计

```
DEEP-05 → B1 POC → Eytzinger POC → [S-tree 条件]
```

| 路径 | 条件 | 结论 |
|------|------|------|
| B1 ≥ 1.5x @ 10M | lo16 跳过在大规模有效 | Phase 2 优先 B1 |
| B1 ≥ 1.5x @ 1.5M 但 10M 回退 | B1 仅小数据有效 | B1 降为 ≤2M 特化路径 |
| Eytzinger ≥ 2x @ 10M | 布局 + branchless 生效 | Phase 2 主推 Eytzinger，可选组合 B1 lo16 |
| Eytzinger ≥ 1.2x 但 < 2x | 布局有正收益但不显著 | 尝试 B1 + Eytzinger 组合 |
| Eytzinger < 1.2x | 布局优势不敌分支预测 | 触发 S-tree POC |
| S-tree ≥ 1.5x | B-tree + SIMD 生效 | 以 S-tree 为 Phase 2 后备 |
| 三方向全 < 1.2x | 没有方案能实质性超越标量 | 触发 RFC，重新评估命题 |

### 算法工程师补充

> 如果三方向全失败，最终方案可能是「标量二分 + Eytzinger 布局」作为终局。这不是失败——用 3 天 POC 避免了 3 周的错误方向。

### 代码安全专家补充

> 以上阈值的正确性必须基于 Phase 1 标量正确版本（`search_scalar.c`），不是 POC v3 缺陷 AVX2 版本。架构项目经理已明确此基准。

---

## 议题 4：POC 执行策略

| 维度 | 共识 |
|------|------|
| 执行方式 | **顺序**，非并行。DEEP-05 输出直接影响后续，Eytzinger 结果决定 S-tree 是否值得做 |
| 时间预算 | **3 天**（DEEP-05 0.5d + B1 0.5d + Eytzinger 1d + S-tree 1d 预留） |
| 代码位置 | `src/poc_*.c`，单文件自包含，与 `poc_benchmark_v3.c` 同级 |
| 构建方式 | 不修改 Makefile，单条 `gcc -O3 -mavx2 -march=native` 编译 |
| 与 Phase 1 收尾的关系 | 先执行 P0 14 项收尾（~1.5h），再启动 POC。不混在一起 |
| 安全最低要求 | ASan + UBSan + 边界数据集（n=0,1,15,16,17,65535,65536,65537）必须零告警 |

### 代码安全专家的安全门控

POC 阶段不满足以下**任何一项**即不允许进入 Phase 2 设计：

1. SIMD tail handling 已实现（不允许留 [DEBT]）
2. dir 数组读写有边界检查
3. `-Wall -Wextra -Wpedantic -Wconversion` 零警告
4. ASan + UBSan 零告警
5. 边界数据集全通过（n=0,1,15,16,17,65535 等非 16 倍数）

### 后端工程师的工程建议

- POC 阶段：单文件自包含，复制 bench 基础设施代码
- POC 通过后：提取到 `build_*.c` / `search_*.c`，加入 `libint32search.a` 构建链路
- 计时改进：统一使用 `__rdtscp() + _mm_lfence()`
