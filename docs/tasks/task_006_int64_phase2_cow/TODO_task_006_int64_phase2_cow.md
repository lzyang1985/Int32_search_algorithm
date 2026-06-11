---
title: 待办清单 — Int64 二期 Phase 2 (COW + Bloom 重建)
status: Archived
created_at: 2026-06-10
updated_at: 2026-06-10
author: Agent_Architect
task_id: task_006_int64_phase2_cow
parent_task: task_005_int64_extension
source_docs:
  - "FINAL_task_006_int64_phase2_cow.md"
  - "meeting_020 D-169"
tags: [todo, post-archive]
---

# 待办清单

## 修复

| # | 内容 | 类型 | 优先级 | 来源 | 说明 |
|---|------|------|--------|------|------|
| ACT-60 | Int64 B1 D-143 防御移植 | 修复/安全 | **P0** | meeting_020 ACT-60 | `search_b1_int64.c` 追加 `if ((size_t)end > n) return (size_t)-1;`。SG-2 安全门禁阻塞项。 |

## 优化

无。

## 配置

无。

## 测试

无。

---

**task_006 全部 11/11 原子任务已 SUCCESS。本 TODO 仅含 1 项 post-archive 安全修复（ACT-60），不阻塞归档。**

本次审计结束，无更多自动处理。
