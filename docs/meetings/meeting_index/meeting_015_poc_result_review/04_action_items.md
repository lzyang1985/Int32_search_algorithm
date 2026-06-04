---
title: 行动项 — Int64 扩展 + Bloom 旁路 POC 结果评审会
meeting_id: meeting_015_poc_result_review
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 行动项 — Int64 扩展 + Bloom 旁路 POC 结果评审会

## 📋 行动项总览

| 编号 | 行动项 | 优先级 | 依赖 | 状态 |
|------|--------|--------|------|------|
| ACT-19 | 更新 POC 报告（D-114 修正项） | P0 | 无 | ✅ Completed |
| ACT-20 | 执行 int64 B1 crossover POC（受控 M 序列 8~8192） | P0 | 无 | ✅ Completed |
| ACT-21 | 人工确认 D-109 新 Go/No-Go 路径 | P0 | 无 | ✅ Completed（人类） |
| ACT-22 | 启动立项 Align 阶段（ALIGNMENT_int64_b1.md） | P0 | ACT-19, ACT-20, ACT-21 | ✅ Completed |
| ACT-23 | 更新 POC 报告补充安全评审章节 | P1 | ACT-19 | ✅ Completed |
| ACT-24 | 更新技术路线文档 — 新增 Int64 二期模块清单 | P1 | ACT-21 | ⏳ Pending |
| ACT-25 | 更新 meeting_index.md — 注册 meeting_015 | P1 | 无 | ✅ Completed |
| ACT-26 | 记录 Eytzinger int64 POC + AVX-512 探索到 Phase 3 | P2 | 无 | ⏳ Pending |

---

## 行动项详情

### ACT-19：更新 POC 报告 [P0]

**目标**：按 D-114 决议修正 [poc_int64_report.md](../../../decisions/poc_int64_report.md)

**交付物**：
1. 状态 `Frozen` → `Reviewing`
2. 新增 §1.1 "GATE-2 新路径解释"
3. 修正 §3.2 B1 阈值：`2000` → `初始保守值 256，Phase 1 内校准`
4. 新增 §3.4 "安全评审摘要"
5. 新增 §5.1 "完整 API 对标清单"
6. 新增 §5.2 "缺失设计决策补充"
7. 标注 DEBT 项：阈值/[COW](#)/[dir 容量](#)

**验收标准**：
- [ ] 7 项修正全部完成
- [ ] DEBT 标注格式符合项目规范

---

### ACT-20：执行 int64 B1 crossover POC [P0]

**目标**：构造受控 M 序列（M ∈ {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192}），精确测定 int64 B1 vs 标量二分的 crossover 点。

**交付物**：
1. `src/poc_int64_b1_crossover.c` — 单文件自包含
2. 输出 `crossover_report`：B1_cy/query 和 Scalar_cy/query 随 M 变化曲线
3. 确定 crossover 点 M*（B1 与 Scalar 交点）
4. 建议阈值 = M* × 0.8（安全余量）

**编译命令**：
```bash
gcc -O3 -std=c11 -mavx2 -o poc_int64_b1_crossover src/poc_int64_b1_crossover.c -lm
```

**验收标准**：
- [ ] M 序列 11 个点全覆盖
- [ ] 每个 M 7 次重复取中位数
- [ ] 输出 crossover 点
- [ ] 建议阈值写入报告

---

### ACT-21：人工确认 D-109 新 Go/No-Go 路径 [P0]

**⚠️ 人类行动项**

**背景**：meeting_014 D-106 决策树定义 GATE-2 为"阻塞性，不通过 → int64 不可行"。POC 中 GATE-2 未通过但 GATE-3 超预期通过（9.17x），meeting_015 D-109 正式确认了一条决策树未覆盖的新路径。

**确认内容**：
- [ ] 接受「GATE-2 FAIL + GATE-3 PASS → Path A=标量二分 + Path B1 主线 → int64 扩展 GO」
- [ ] 确认此决策变更不影响 int32 产品线
- [ ] 授权启动立项流程

---

### ACT-22：启动立项 Align 阶段 [P0]

**目标**：按 D-112 条件启动 int64 二期立项

**前置条件**：
- [ ] ACT-19 ✅（POC 报告已更新）
- [ ] ACT-20 ✅（crossover POC 完成）
- [ ] ACT-21 ✅（人工确认）

**交付物**：
1. `docs/立项文档路径待定/ALIGNMENT_int64_b1.md`
2. `docs/立项文档路径待定/CONSENSUS_int64_b1.md`

**输入文档**：
- POC 报告（修正后）
- meeting_012/013/015 决议
- 总需求文档 + 技术路线文档

---

### ACT-23：补充 POC 报告安全评审章节 [P1]

**目标**：在 POC 报告中新增独立安全评审摘要

**内容**：
- Security 专家评审的 6 项发现（SEC-POC-01~06）
- meeting_013 已知问题（SEC-B1/B2/B3）在 POC 环境的状态声明
- 内存安全声明（ASan/UBSan 覆盖范围）
- 进入 Execute 阶段的安全前置条件清单

**验收标准**：
- [ ] §3.4 安全评审摘要写入 POC 报告

---

### ACT-24：更新技术路线文档 [P1]

**目标**：在 [技术路线.md](../../../architecture/技术路线.md) 中新增或更新 Int64 二期相关内容

**交付物**：
1. 新增 §10 "二期工程 Int64 扩展路线"
   - 记录 D-108 ~ D-114 决议摘要
   - int64 Phase 1 修正后模块清单
   - B1 阈值校准策略（保守初始值 256 + Phase 1 内校准）
   - 独立库 libint64search 架构确认
2. 删除或标注废弃 meeting_012 D-095 "Path A Only MVP" 规划

**验收标准**：
- [ ] §10 新增完成
- [ ] 与 D-110 模块清单一致
- [ ] 标注待 Phase 1 校准的 DEBT 项

---

### ACT-25：更新 meeting_index.md [P1]

**目标**：在会议总目录中注册 meeting_015

**交付物**：
1. `meetings_registry` 中新增 meeting_015 条目
2. 已完成会议表格中新增一行
3. `last_updated` 更新为 2026-06-02

---

### ACT-26：Phase 3 探索项记录 [P2]

**目标**：将长远探索项记录到技术路线

**交付物**：
1. 技术路线 §10 记录：
   - `[P2] int64 Eytzinger 布局 POC benchmark`
   - `[P2] AVX-512 8 路 int64 二分探索`
2. 记录触发条件（Phase 1 完成后根据 B1 实际表现决定是否启动）

---

## 📅 建议执行顺序

```
立即:
  ACT-19  POC 报告更新（7 项修正）
  ACT-25  meeting_index.md 更新

Day 1:
  ACT-20  int64 B1 crossover POC（约 2-4 小时计算）
  ACT-23  安全评审章节（与 ACT-19 同步）

等待:
  ACT-21  人工确认（阻塞性，等待人类响应）

人工确认后:
  ACT-24  技术路线更新
  ACT-22  立项 Align 阶段启动
  ACT-26  Phase 3 探索项记录
```

---

## 关联信息

- 源会议：meeting_015_poc_result_review
- 决议文档：[03_decisions.md](03_decisions.md)
- 讨论记录：[02_discussion.md](02_discussion.md)
- 前置行动项：meeting_014 ACT-14~18 ✅（全部完成）
