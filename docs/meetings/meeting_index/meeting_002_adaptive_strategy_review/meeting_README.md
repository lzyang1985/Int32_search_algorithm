---
title: 自适应策略评审会 — 单一算法 vs 桶大小自适应
meeting_id: meeting_002_adaptive_strategy_review
status: Frozen
created_at: 2026-05-27
updated_at: 2026-05-30
author: Human
parent_doc: docs/meetings/meeting_index.md
parent_task: task_001_phase1_mvp
participants: [Architect, Algorithm, Backend, Fullstack, Security]
---

# 自适应策略评审会：单一算法 vs 桶大小自适应

## 元信息
- **会议 ID**: meeting_002_adaptive_strategy_review
- **当前状态**: ✅ Frozen（人工确认签收）
- **关联任务**: root_kickoff（待建立顶层任务）
- **父文档**: docs/meetings/meeting_index.md

## 文档状态看板
| 文档名 | 状态 | 最后更新 | 说明 |
|--------|------|----------|------|
| 01_agenda.md | ✅ Frozen | 2026-05-30 | 会议议程 |
| 02_discussion.md | ✅ Frozen | 2026-05-30 | 三轮讨论记录（含人工修正方案） |
| 03_decisions.md | ✅ Frozen | 2026-05-30 | D-010~D-019；D-015 经 POC v3 修正为 max_bucket ≤ 150 |
| 04_action_items.md | ✅ Frozen | 2026-05-30 | 待办事项；B1 POC + prev_h 已完成 |
| 07_poc_benchmark_v3_report.md | ✅ Frozen | 2026-05-30 | D-019 完成：crossover ≈ 1.6M，阈值 max_bucket ≤ 150 |

## 决议摘要

**最终结论：批准构建时一次性选路方案。**

### 第一轮（已弃用）
- D-010：拒绝每查询自适应多路径 (5/5)
- D-011：方案 A 为唯一查询路径 (5/5)
- D-012：B1 1M 优势记录为 trade-off (5/5)
- D-013：立即进入立项 (5/5)

### 第三轮（人工修正，当前生效）
1. **D-014**：批准构建时一次性选路方案（A + B1 两条路径，构建时决策）(5/5)
2. **D-015**（D-019 修正后）：分布检测规则 `max_sz > 0.1×n → PATH_A` / `max_bucket ≤ 150 → PATH_B1` / else → PATH_A (5/5)
3. **D-016**：prev_h=0xFFFF bug 修复 + dir 一致性校验；v3 已用 `first=1` 标记法修复 (5/5) ✅
4. **D-017**：B1 路径 COW 需 struct 级原子交换 (5/5)
5. **D-018**：D-003 条件豁免（分布验证后启用分位策略，条件 `max_bucket ≤ 150`）(5/5)
6. **D-019**：阈值 POC 校准完成 ✅。crossover ≈ 1.6M，`max_bucket ≤ 150` 全对 (5/5)

### 方案全貌（最终确定）

```
Int32 查找算法
├── create() 构建阶段
│   ├── 排序数据
│   ├── 构建 high16 dir（临时，262KB）+ 一致性校验
│   ├── 分布分析：max_sz > 0.1×n ? max_bucket ≤ 150 ?
│   ├── → PATH_B1: 构建 lo16 数组（+50% 内存），保留 dir
│   └── → PATH_A:  释放 dir，仅保留 vals（40MB / 10M）
│
├── search() 查询阶段（零开销 dispatch）
│   ├── PATH_B1: high16 dir O(1) → lo16 SIMD 等值扫描
│   │   └── 性能：1M=75 cy（2.1x vs A），1.6M crossover≈135 cy
│   └── PATH_A:  AVX2 8 路块状二分
│       └── 性能：5M=146 cy, 10M=172 cy（3.5x-5.1x vs 标量）
│
├── 性能速览
│   ├── 1M ~ 1.6M uniform:  B1 0.89x-2.1x vs A
│   ├── 1.6M ~ 10M uniform: A 1.18x-5.1x vs 标量
│   └── Skewed / 倾斜:      A 自动回退（max_sz > 0.1×n）
│
├── 扩展
│   ├── AVX-512（A路径 3行 / B1路径 2行，独立）
│   └── 布隆过滤器（编译开关，两路径均可用）
│
└── 舍弃（两轮POC + 三轮会议证伪）
    ├── 有序链表（→ D-001 改为排序数组）
    ├── 锚点索引 / 无条件 lo16 / 无条件 high16 dir
    ├── 每查询自适应多路径（→ D-010 否决）
    └── mid8 二级目录（→ D-010 否决）
```

## 待办事项
| 编号 | 事项 | 负责人 | 状态 |
|------|------|--------|------|
| B-01 | 阈值 POC 校准（1.5M/2M/2.5M/3M） | Agent | ✅ 完成（D-019，max_bucket ≤ 150） |
| B-02 | 立项工作流：ALIGNMENT + CONSENSUS | Architect Agent | Pending |
| B-03 | 立项工作流：DESIGN | Architect Agent | Pending |
| B-04 | 立项工作流：TASK | Architect Agent | Pending |
| C-01 | AVX2 vs AVX-512 性能对比 | Algorithm Agent | Pending |
| C-02 | 修复 prev_h bug | Agent | ✅ 完成（v3 first=1 标记法，D-016） |
