---
title: 剩余待办事项收尾路线确认会
meeting_id: meeting_020_todo_roadmap_confirmation
status: Reviewing
created_at: 2026-06-10
updated_at: 2026-06-10
author: Agent_Executor
parent_doc: docs/meetings/meeting_index.md
parent_task: root
participants: [Architect, Backend, Algo, Sec, Fullstack]
---

# 剩余待办事项收尾路线确认会

## 元信息

| 属性 | 值 |
|------|-----|
| **会议 ID** | meeting_020_todo_roadmap_confirmation |
| **当前状态** | Reviewing（待人工确认 Frozen） |
| **创建时间** | 2026-06-10 |
| **父文档** | docs/meetings/meeting_index.md |
| **关联任务** | root |
| **参与者** | Architect, Backend, Algo, Sec, Fullstack |

## 背景

项目已完成6个开发任务（task_001~006），19个会议中16个已冻结。当前剩余28项行动项分散在 meeting_016/017/018/019 中，以及 demo 文件 V1-V4 经历多次修改反复调整。现需讨论确认：哪些待办确实需要做，哪些可以关闭。

## 文档状态看板

| 文档名 | 状态 | 最后更新 | 来源 |
|--------|------|----------|------|
| 02_discussion.md | 👀 Reviewing | 2026-06-10 | 五专家并行讨论 + 4次交叉裁决 |
| 03_decisions.md | 👀 Reviewing | 2026-06-10 | D-165~D-174（10项决议） |
| 04_action_items.md | 👀 Reviewing | 2026-06-10 | 14 项行动项（ACT-60~ACT-74） |

## 决议摘要

### 核心决议（10项，5/5通过）
- **D-165**：28项"待办"去重为16项唯一行动项（U-01~U-16）
- **D-166**：V4概念正式消解（meeting_019修复后V1/V4等价），bench_100.ps1简化
- **D-167**：收尾路线 Phase A'（安全+工程基础8项，~4.5天）+ Phase B（条件触发6项）
- **D-168**：维护模式门禁修订为 G6-minimal（安全闭合+功能完整，不含性能POC）
- **D-169**：task_006 立即归档（11/11 SUCCESS，全平台验证通过）
- **D-170**：安全 P0 — Int64 D-143 防御移植立即执行（30min）
- **D-171**：CMakeLists.txt 删除（过时）
- **D-172**：Rust RAII 示例取消
- **D-173**：6 项行动项正式关闭（U-04/U-09/U-12/U-14/U-15/U-16）
- **D-174**：Phase A' 完成后 meeting_016/017/018 全部 Frozen

### 交叉裁决（4项，中立第三方裁定）
- **C-1 (PGO+LTO)**：PGO 保留 Phase A'（1天），LTO opt-in（0.5天），64B对齐立即独立
- **C-2 (find_with_hint)**：维持 P1，附 benchmark 验收门 + `@experimental`
- **C-3 (预取)**：分阶段 Gate 推进，L3 miss<5% 直接关闭
- **C-4 (D-140重验证)**：30min 时间盒，二元结局（合并 or 永久归档）

## 待办事项（Phase A' 核心）

| 编号 | 待办内容 | 负责人 | 优先级 | 工作量 | 状态 |
|------|----------|--------|--------|--------|------|
| ACT-60 | Int64 B1 D-143 防御移植 | Sec | **P0** | 30min | 待执行 |
| ACT-61 | Int64 D-142 小桶移植 | Algo | P1 | 30min | 待执行 |
| ACT-62 | Dir fuzz 基础覆盖率 | Sec | P1 | 1天 | 待执行 |
| ACT-63 | bench_100.ps1 方法论增强 | Fullstack | P1 | 1h | 待执行 |
| ACT-64 | 64B 对齐独立实施 | Backend | P1 | 5min | 待执行 |
| ACT-65 | Huge Pages 快速验证 | Algo | P1 | 2h | 待执行 |
| ACT-66 | PGO Makefile target | Backend | P1 | 1天 | 待执行 |
| ACT-67 | find_with_hint 最小原型 | Fullstack | P1 | 2天 | 待执行 |
| ACT-68 | task_006 归档 | DocMgr | P1 | 30min | 待执行 |

## 增量日志指向

- `_incremental/2026-06-10_meeting_020.yaml`
