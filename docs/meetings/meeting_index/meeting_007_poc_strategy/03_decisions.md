---
title: 决议 — POC 策略讨论
meeting_id: meeting_007_poc_strategy
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_007_poc_strategy/meeting_README.md
parent_task: task_001_phase1_mvp
---

# 决议 — POC 策略讨论会

---

## D-053：POC 优先级排序

**决议**：四个候选方向按以下优先级顺序执行 POC：

| 优先级 | 方向 | 代码量 | 耗时 | 验收标准 |
|:---:|------|:---:|:---:|------|
| P0 | DEEP-05 | 0 行 | 0.5d | 输出瓶颈 cycle 占比表 |
| P0 | Eytzinger | ~250 行 | 1d | 10M 均匀 ≥ 2.0x vs 标量 |
| P0 | Path B1 | ~已有 | 0.5d | 1.5M 均匀 ≥ 1.5x vs 标量 |
| P1 | S-tree | ~310 行 | ~1d | 条件触发（Eytzinger < 2x 时） |

**投票**：4/4 通过。

---

## D-054：POC 顺序执行，非并行

**决议**：POC 按 DEEP-05 → B1 → Eytzinger → [S-tree 条件] 顺序执行。DEEP-05 输出直接影响后续 POC 设计；并行会分散注意力且方向间有交叉启发。

**投票**：4/4 通过。

---

## D-055：POC 代码落地策略

**决议**：
1. **位置**：`src/poc_eytzinger.c`、`src/poc_stree.c`，与现有 `poc_benchmark_v3.c` 同级
2. **模式**：单文件自包含（~250-310 行），复制 bench 基础设施代码
3. **构建**：不修改 Makefile，单条 `gcc -O3 -mavx2 -march=native` 编译，命令写入 README.txt
4. **通过后**：提取核心逻辑到 `build_*.c` / `search_*.c`，加入 `libint32search.a` 构建链路
5. **失败后**：保留 `poc_*.c` 并标注 `// ARCHIVED: see meeting_007 D-xxx`

**投票**：4/4 通过。

---

## D-056：POC 验收基准统一

**决议**：所有 POC 的性能基准统一为 **Phase 1 交付代码的标量二分正确版本**（`search_scalar.c`），不是 POC v3 的缺陷 AVX2 版本。FINAL §3 的 POC v3 数据（5.26x）不作为任何 POC 的对比基准。

| 方向 | 数据规模 | 分布 | 命中率 | 成功阈值 | 失败阈值 | 基准 |
|------|----------|------|--------|----------|----------|------|
| Eytzinger | 10M | uniform | 50% | ≥2.0x | <1.2x | Phase 1 标量 |
| B1 | 1.5M, 10M | uniform, skewed | 50% | ≥1.5x | <1.2x | Phase 1 标量 |
| S-tree | 10M | uniform | 50% | ≥1.5x | <1.2x | Phase 1 标量 |

**投票**：4/4 通过。

---

## D-057：POC 安全门控

**决议**：POC 阶段必须满足以下全部条件，才允许进入 Phase 2 设计：

| # | 条件 | 检查方式 |
|:--:|------|----------|
| 1 | SIMD tail handling 已实现（不允 [DEBT]） | 代码审查 |
| 2 | dir 数组读写有边界检查 | Debug assert |
| 3 | `-Wall -Wextra -Wpedantic -Wconversion` 零警告 | 编译期 |
| 4 | ASan 零告警 | `-fsanitize=address` |
| 5 | UBSan 零告警 | `-fsanitize=undefined` |
| 6 | 边界数据集全通过 | n=0,1,15,16,17,65535,65536,65537 |

任何一项不满足即**阻塞** Phase 2。

**投票**：4/4 通过。

---

## D-058：go/no-go 决策树

**决议**：采用以下决策树指导 Phase 2 方向选择：

```
B1 POC:
  ≥1.5x @ 10M    → Phase 2 主推 B1
  ≥1.5x @ 1.5M   → B1 为 ≤2M 特化路径
  <1.2x @ 1.5M   → 跳过 B1，继续 Eytzinger

Eytzinger POC:
  ≥2.0x @ 10M    → Phase 2 主推 Eytzinger，可选组合 B1 lo16
  ≥1.2x 但 <2.0x → 尝试 B1+Eytzinger 组合
  <1.2x          → 触发 S-tree POC

S-tree POC (条件):
  ≥1.5x @ 10M    → Phase 2 主推 S-tree
  <1.5x          → 触发 RFC，终局方案：「标量二分 + Eytzinger 布局」

三方向全 < 1.2x:
  → 触发 RFC 重新评估「int32 有序数组 SIMD 加速」命题
  → 或接受 1.2-1.5x 适度加速为终局目标
```

**投票**：4/4 通过。

---

## D-059：Phase 1 收尾 → POC 的顺序关系

**决议**：P0 14 项收尾（~1.5h）必须先于 POC 执行。Phase 1 Frozen 不在 POC 之前宣告，但 POC 可在收尾完成后立即启动，不等待 DOC-01~04。

**投票**：4/4 通过。

---

> **本次会议 7 项决议全票通过（4/4）。**
> 会议状态：Draft → Reviewing，待人工确认后 Frozen。
