---
title: 布隆过滤器开关特性讨论会议
meeting_id: meeting_013_bloom_toggle
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
parent_doc: docs/meetings/meeting_index.md
parent_task: root
participants: [Architect, Algorithm, Backend, Fullstack, Security]
---

# 会议仪表盘：布隆过滤器开关特性讨论

## 元信息
- **会议 ID**: meeting_013_bloom_toggle
- **标题**: 布隆过滤器开关（Bloom Filter Toggle）特性讨论
- **目标**: 讨论用户可自行决定是否启用布隆过滤器的特性设计、影响与可行性
- **关联任务**: root（顶层会议）

## 文档状态看板
| 文档名 | 状态 | 最后更新 | 说明 |
|--------|------|----------|------|
| 01_agenda.md | ✅ Frozen | 2026-06-02 | 会议议程 |
| 02_discussion.md | ✅ Frozen | 2026-06-02 | 讨论记录（两轮交叉讨论，冲突已解决） |
| 03_decisions.md | ✅ Frozen | 2026-06-02 | 决议记录（D-097~D-101，5项全票通过） |
| 04_action_items.md | ✅ Frozen | 2026-06-02 | 行动项（7项，P0×2/P1×2/P2×2/P3×1） |

## 参与 Agent
| Agent | 角色 | 职责 |
|-------|------|------|
| Architect-Project-Manager | 架构项目经理 | 仲裁最终决议、评估架构影响 |
| Algorithm-Engineer | 算法工程师 | 评估布隆过滤器算法层面的影响 |
| Backend-Engineer | 后端工程师 | 评估 API、内部实现、数据结构影响 |
| Fullstack-Dev | 全栈开发 | 评估工程成本、构建系统、测试 |
| Code-Security-Expert | 代码安全专家 | 评估内存安全、并发安全风险 |

## 背景

当前 libint32search 在编译期通过 `INT32_SEARCH_USE_BLOOM` 宏开关控制布隆过滤器的编译，
在 API 调用期通过 `int32_search_config_t.use_bloom` 字段决定是否创建布隆过滤器。
一旦创建后，所有查询调用都会先通过布隆过滤器预筛。

本次会议讨论：是否应该、以及如何允许用户在运行时动态开关布隆过滤器（或某些查询绕过布隆过滤器），
以便对"确定一定存在"的查找需求提供更友好的支持。
