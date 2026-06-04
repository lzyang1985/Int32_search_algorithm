---
title: meeting_009 Step 3 偏差记录 — DEV-001~003
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Executor
task_id: task_001_phase1_mvp
parent_doc: docs/tasks/task_001_phase1_mvp/ACCEPTANCE_task_001_phase1_mvp.md
source_docs:
  - docs/meetings/meeting_index/meeting_010_crossover_results_review/03_decisions.md
  - docs/meetings/meeting_index/meeting_010_crossover_results_review/04_action_items.md
tags: [deviation, acceptance, phase2, debt]
---

# meeting_009 Step 3 偏差记录 — DEV-001~003

> 依据：D-084 决议、DOC-03 行动项
> 来源：meeting_010 crossover 评审会讨论记录（02_discussion.md PC-05）

---

## 偏差清单

| 编号 | 严重度 | 分类 | 描述 | 原因 | 处理方式 |
|:--:|:------:|------|------|------|----------|
| **DEV-001** | Minor | 功能实现偏差 | stdout 打印缺少 D-074 要求的 `stddev`/`min`/`max` 列；CSV 输出只有 median 和 speedup，未包含完整统计量 | Step 3 实现时以 `printf` 简洁输出为优先，统计量虽已计算但未写入 CSV | 标注 [DEBT]，不影响 CROSS-01~05 签收。Phase 2 实施时补充 |
| **DEV-002** | Minor | 性能/复杂度偏差 | 10M skewed `pool` 测试 Step 2 (253.9 cy/q) 与 Step 3 (190.1 cy/q) 偏差 33%，归因未验证 | 疑似 LCG seed 差异导致 max_bucket 相同(4121)但数据分布细节不同；或两次运行间服务器负载波动（Step 3 测得较晚更稳定） | D-082 决议以同一进程相同 seed 重测验证；处理记录见本报告 §3 |
| **DEV-003** | Minor | 接口偏差 | D-015 阈值 "max_bucket ≤ 2000" 未区分受控构造（B 级）与自然分布（A 级）语义差异 | 受控构造 crossover 点精确为 2000；自然分布因 skewed 更紧凑实际容许 >2000 | 文档中说明差异，建议以受控构造阈值（保守）为准；偏差仅影响文档精确度 |

---

## DEV-001 详细说明

### 偏差内容
`poc_b1_crossover.c` 的 `bench_crossover_one()` / `bench_natural_one()` 函数：
- 内部已计算 `bench_stats_t`（含 `median`/`min`/`max`/`stddev`）
- stdout 仅输出 `median` 和 `speedup`
- CSV 仅输出 `median` 和 `speedup`

### 影响范围
- CROSS-04: "CSV 输出：n,max_bucket,pool_cy,3ptr_cy,scalar_cy,pool_vs_scalar,3ptr_vs_scalar" — 缺少 stddev/min/max 列
- 不影响核心正确性验证和 crossover 点判定

### 修复建议 [DEBT]
- CSV header 增加 `pool_stddev,3ptr_stddev,scalar_stddev,pool_min,pool_max,...` 列
- stdout 末尾追加 `±stddev` 标注
- 优先级：P2（Phase 2 实施时一并完成）

---

## DEV-002 详细说明

### 偏差内容
10M skewed 分布 `pool` 算法在 Step 2 (meeting_009) 与 Step 3 (同 meeting) 分别测得：
- Step 2: 253.9 cy/q（出自 `poc_b1_pool_vs_3ptr.c`，run_benchmark_natural）
- Step 3: 190.1 cy/q（出自 `poc_b1_crossover.c`，bench_natural_one）
- 偏差: 33%

### 可能原因
1. **LCG seed 差异**: 两次运行使用不同 seed，导致 10M skewed 数据细节不同（即使 max_bucket 相同）
2. **服务器负载**: Step 3 较晚测得，可能当时服务器更空闲
3. **基准框架差异**: Step 2 使用 `poc_b1_pool_vs_3ptr.c`，Step 3 使用 `poc_b1_crossover.c`，缓存冲刷策略可能不同

### 处理结果
参见 D-082 / DOC-04：以同一进程、同一 LCG seed 在 `poc_b1_crossover.c` 中重测 10M skewed。重测结果见 [06_crossover_report.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_009_poc_execution_plan/06_crossover_report.md) §4.3 偏差备注更新。

---

## DEV-003 详细说明

### 偏差内容
D-015 决策规则修正为 `max_sz ≤ 2000 → PATH_B1`，此值源于受控构造 B 级验证：
- 受控构造（B 级）: crossover ≈ M=2000，此时 max_bucket 精确等于 2000
- 自然分布（A 级）: skewed 分布 10M 时 max_bucket=4121 仍保持 1.3x 加速比，意味 crossover 远超 2000

### 影响
以受控构造阈值（2000）作为单一路径判定标准是保守策略——可能在某些自然 skewed 场景下误判为 PATH_A，但保证不会因高估而退化。

### 处理
- 文档中已注明阈值来源为 "受控构造 B 级验证"
- `weighted_avg` 预留字段（D-080）未来可用于精细调优
- 不影响 Phase 2 实施

---

## 签收状态

以上 3 项偏差均为 **Minor** 级别，其中：
- DEV-001 标注 [DEBT]，Phase 2 实施时处理
- DEV-002 已通过 D-082 重测处理
- DEV-003 仅影响文档精确度，不阻塞开发

**meeting_009 Step 3 偏差记录完毕。**
