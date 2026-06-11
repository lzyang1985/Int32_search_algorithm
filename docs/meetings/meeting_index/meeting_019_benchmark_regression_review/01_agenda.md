---
title: V3 性能回归紧急评审 — 议程
meeting_id: meeting_019_benchmark_regression_review
status: Frozen
created_at: 2026-06-09
updated_at: 2026-06-09
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_019_benchmark_regression_review/meeting_README.md
parent_task: root
---

# 议程：V3 性能回归紧急评审会

## 议题1: 实测数据与 meeting_018 结论为何严重矛盾？

**背景**：
- meeting_018 对 V3 的结论是"略慢更稳定，<0.5% 噪声地板内，D-142 ~2.1cy 不可辨识，D-143 <<1cy 不可测"
- @Host 在干净环境下实测：V3 在 Int64 场景慢 12.11%，Bloom OFF 慢 8.86%，全命中慢 5.88%
- 只有 Int32 default 场景与 meeting_018 一致

**讨论要点**：
1. meeting_018 的数据是在什么环境下采集的？是否足够干净？
2. @Host 强调"任何额外操作都会导致极大偏差" — 这是否暴露了 meeting_018 的环境问题？
3. D-142/D-143 是否在特定场景下有隐藏的、非均匀的性能代价？

---

## 议题2: Int64 场景 — max 0.29→0.63us 尖刺根因

**背景**：
- V1 Int64: min 0.10, max 0.29, avg 0.14
- V3 Int64: min 0.11, max 0.63, avg 0.16
- Max 翻倍（0.29→0.63），avg 慢 12.11%
- meeting_018 已知 Int64 B1 缺少 D-143 等效防御（Sec HIGH），但 V3 已经加了什么？

**讨论要点**：
1. V3 Int64 到底改了什么？（D-148 尚未实施）
2. 0.63us 尖刺是否来自 D-143 防御检查的分支预测失败？
3. 还是 V3 代码布局扰动（Heisenberg 效应）在 Int64 路径更致命？

---

## 议题3: Bloom OFF 系统性变慢的根因

**背景**：
- Bloom OFF 50% hit: V3 慢 8.86%（0.14us vs 0.13us）
- Bloom OFF 100% hit: V3 慢 5.88%（0.05us vs 0.04us）
- D-140 在 Bloom OFF 50% 场景曾被检测到 +25.7% 回归（meeting_017 修订）

**讨论要点**：
1. D-140 虽已 `#ifdef` 关闭，但其他 D-141/D-142/D-143 是否有交叉影响？
2. 代码布局变化（D-142 注入新函数路径）是否导致了 I-cache / 分支预测器污染？
3. 为什么全命中场景（0.04→0.05us）也在变慢？全命中应该走最快的路径

---

## 议题4: "内存瓶颈+硬件压榨"环境下的重新评估

**背景**：
- @Host 明确指出：瓶颈在内存，对硬件压榨得狠
- 任何额外操作都导致测试偏差
- meeting_017 已知 B1 路径 88% 时间在内存子系统

**讨论要点**：
1. 当前优化（D-140~D-143）是否在"计算层面"微优化，却加剧了"内存层面"的问题？
2. 代码膨胀（D-142 新分支 + D-143 防御检查）是否导致 I-cache miss 增加？
3. 在极限压榨环境下，是否需要重新审视"每 1 cycle 都要省"的策略？

---

## 议题5: 行动决策

**选项**：
- A) 接受现状 — V3 Int32 default 场景持平，其他场景退化可接受
- B) 回退 D-142/D-143（保留 D-141 对齐），重新评估
- C) 全部回退 D-140 系列，从干净基线重新开始
- D) 仅在特定场景回退（Int64 路径回退，Int32 保留）
- E) 其他

**约束**：
- meeting_017 D-131/D-132/D-133 四个 POC 尚未执行
- meeting_018 D-153 三阶段收尾路线已规划但未启动
