---
title: 决议文档 — meeting_009 POC 执行结果评审会
meeting_id: meeting_010_crossover_results_review
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_010_crossover_results_review/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_010_crossover_results_review/02_discussion.md
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/06_crossover_report.md
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/03_decisions.md
  - docs/requirements/总需求文档.md
tags: [review, crossover, b1, decision, phase2]
---

# 决议文档 — meeting_009 POC 执行结果评审会

> 4/4 专家参与。以下决议基于四位专家独立的书面评审意见及交叉讨论形成的共识。

---

## D-078：Crossover 数据评审通过，阈值修正为 max_bucket ≤ 2000

**投票**：4/4 通过

**决议内容**：
- meeting_009 的全部交付物（POC 三文件 + 两份 benchmark 报告）核心结论**可信且可操作**
- D-015/D-019 原阈值 `max_bucket ≤ 150` 基于 BUG-02 污染数据，修 bug 后重新测量确定真值
- 新阈值为 **`max_bucket ≤ 2000`**（基于 B 级受控构造 crossover 实测）

**约束**：
- 阈值 2000 与测试 CPU（Xeon Gold 6152, L3=30.3MiB）关联，跨硬件可移植性待 Phase 3 验证
- Phase 2 v1.0 设计文档中标注此硬件依赖
- 保留运行时自适应阈值的扩展点（`weighted_avg` 字段预留）

**影响范围**：
- `build_decision.c`：一行常量修改
- `总需求文档.md` §6.3：验收标准同步更新
- `技术路线文档` §3.3：D-015 决策规则更新

---

## D-079：B1 热路径采用三指针方案，内存池降级为内部实现细节

**投票**：4/4 通过

**决议内容**：
- `search_b1()` 对外接口约定为**三指针签名**：`(const int32_t *vals, const uint16_t *lo16, const int32_t *dir, size_t n, int32_t target)`
- 内存池方案（`uint8_t *pool`）**不暴露到查询热路径**
- Pool 降级为 `build_b1.c` 内部实现的可选策略（单次 `_mm_malloc` 构建，提取三指针存入 snapshot）
- D-075 声明"内存池消除指针税"的目标**未达成**（实测 pool 慢 5~15%）

**影响范围**：
- `search_b1.c` 函数签名（三指针）
- `b1_snapshot_t` 保持 4 字段结构不变（已兼容三指针）
- `build_b1.c` 内部可使用 pool 方式分配（工程简化），对外暴露独立指针

---

## D-080：Phase 2 v1.0 决策函数采用单阈值（Phase 2 v1.0），保留多指标扩展点

**投票**：3/4 通过（算法工程师保留意见：建议 Phase 2 直接引入 `weighted_avg` 双指标）

**决议内容**：
- Phase 2 v1.0 构建时选路逻辑使用**单一阈值 `max_bucket ≤ 2000`**
- 保留 `weighted_avg = Σ bucket_size² / n` 的计算和存储位置（`uint32_t weighted_avg` 字段），暂不参与决策
- Phase 3 基于生产数据验证后，可升级为：
  ```
  IF max_bucket ≤ 2000  → PATH_B1
  IF max_bucket ≤ 5000 AND weighted_avg ≤ 1000  → PATH_B1  (skewed 场景)
  ELSE  → PATH_A
  ```

**理由**：
- 单阈值覆盖了绝大多数实际有利场景（uniform n≤5M, skewed n>10M 中 max_bucket≤2000 的部分）
- skewed 10M 的 1.33x 收益属于"锦上添花"，不引入额外复杂度
- `weighted_avg` 在构建时零可感知开销（一次 dir 遍历，O(65536)）

---

## D-081：安全补充验证为 Phase 2 签收前置条件

**投票**：4/4 通过

**决议内容**：
以下 3 项补充验证必须在 meeting_009 交付物签收前完成：

| 编号 | 验证项 | 方法 |
|:--:|------|------|
| SV-01 | `_mm_malloc` NULL 检查 | 代码审查：`poc_b1_crossover.c` L411-412, L522-523, L617, L657 加 NULL 检查 |
| SV-02 | INT32_MIN 边界键 | 修改 `verify_boundary_keys()` 加入 `INT32_MIN`，与 `bsearch` 交叉验证 |
| SV-03 | ASan/UBSan @ `--verify` | `gcc -O1 -g -fsanitize=address,undefined ...` 运行 `--verify` 零告警 |

**可选补充（标注 [DEBT]，不阻塞签收）**：
| 编号 | 验证项 |
|:--:|------|
| SV-04 | ASan/UBSan @ n=65535,65536,65537 |
| SV-05 | ASan/UBSan @ n=0,1,5,63,64,65（BUG-04 回归） |

**影响范围**：
- `src/poc_b1_crossover.c`：6 行 NULL 检查 + 1 个边界键添加
- 验证命令运行 + 结果记录

---

## D-082：10M skewed 偏差 33% 需重测确认

**投票**：4/4 通过

**决议内容**：
- 10M skewed 场景下 Step 2（253.9 cy/q）与 Step 3（190.1 cy/q）偏差 33%，**不能简单归因于 LCG seed**
- 在同一进程中用相同 LCG seed、同时测量 Step 2 和 Step 3 配置
- 增加 repeats 至 11 次（discard 前 5）
- 重测属于 P1，可与 Phase 2 实施并行
- 如果重测后偏差 < 10%，确认噪声；如果仍 > 20%，需排查代码差异

---

## D-083：总需求文档 §6.3 验收标准同步修正

**投票**：4/4 通过

**决议内容**：

原标准：
> 1.5M 均匀数据自动选中 B1（max_bucket <= 150）

修正为：
> 1.5M 均匀数据自动选中 B1（max_bucket <= 2000）

**理由**：1.5M uniform 实测 max_bucket=824，按旧阈值 `≤150` 会被判定为 PATH_A，但实测 B1 以 2.50x 胜出。旧标准已与实测数据矛盾。

---

## D-084：会议交付物偏差记录

**投票**：4/4 通过

**决议内容**：
记录以下 3 项 Minor 偏差：

| 编号 | 严重度 | 内容 | 处理 |
|:--:|:------:|------|------|
| DEV-001 | Minor | stdout/CSV 缺少 D-074 要求的 stddev/min/max 列 | 标注 [DEBT]，不影响签收 |
| DEV-002 | Minor | 10M skewed 偏差 33% 归因未验证 | D-082 重测解决 |
| DEV-003 | Minor | 阈值 2000 未区分受控构造 vs 自然分布语义 | 文档中说明差异 |

---

> 以上决议待人工审阅确认 Frozen。确认后将生成 04_action_items.md 详细行动清单。
