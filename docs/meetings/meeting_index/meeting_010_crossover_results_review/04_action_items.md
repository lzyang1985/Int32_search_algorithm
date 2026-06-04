---
title: 行动清单 — meeting_009 POC 执行结果评审会
meeting_id: meeting_010_crossover_results_review
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_010_crossover_results_review/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_010_crossover_results_review/03_decisions.md
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/06_crossover_report.md
tags: [review, crossover, b1, action-items, phase2]
---

# 行动清单 — meeting_009 POC 执行结果评审会

> 会议产出 7 项决议（D-078~D-084），以下为从决议派生的可执行行动项。

---

## Step 1：meeting_009 交付物签收 — 安全补充验证

| 编号 | 决议 | 行动 | 优先级 | 状态 |
|:--:|:--:|------|:------:|:--:|
| SV-01 | D-081 | `poc_b1_crossover.c` L411-412, L522-523, L617, L657 处 `_mm_malloc` 加 NULL 检查，失败后安全返回/exit | P0 | ✅ |
| SV-02 | D-081 | `verify_boundary_keys()` 加入 `INT32_MIN` 边界键，与 `bsearch` 交叉验证 | P0 | ✅ |
| SV-03 | D-081 | `gcc -O1 -g -fsanitize=address,undefined -std=c11 -mavx2 -march=native -fno-tree-vectorize src/poc_b1_crossover.c -o poc_b1_crossover_asan && ./poc_b1_crossover_asan --verify` 零告警 | P0 | ✅ |
| SV-04 | D-081 | ASan/UBSan @ n=65535,65536,65537 验证 | P2 | ✅ |
| SV-05 | D-081 | ASan/UBSan @ n=0,1,5,63,64,65 验证（BUG-04 回归确认） | P2 | ✅ |

---

## Step 2：文档同步更新

| 编号 | 决议 | 行动 | 优先级 | 状态 |
|:--:|:--:|------|:------:|:--:|
| DOC-01 | D-083 | 更新 `总需求文档.md` §6.3："1.5M 均匀数据自动选中 B1（max_bucket <= **2000**）" | P0 | ✅ |
| DOC-02 | D-078 | 更新 `技术路线文档`（如存在）§3.3：D-015 决策规则同步 150→2000 | P0 | ✅ |
| DOC-03 | D-084 | 创建 `docs/tasks/task_001_phase1_mvp/ACCEPTANCE_meeting009_step3.md`，记录 DEV-001~003 偏差及处理方式 | P1 | ✅ |
| DOC-04 | D-082 | 重测 10M skewed 后，更新 `06_crossover_report.md` 偏差备注 | P1 | ✅ |

---

## Step 3：Phase 2 启动前置

| 编号 | 决议 | 行动 | 优先级 | 状态 |
|:--:|:--:|------|:------:|:--:|
| RFC-01 | D-078 | 创建 RFC 记录 `max_bucket ≤ 150 → 2000` 阈值变更（需求变更控制规范） | P1 | ✅ |
| PH2-01 | D-078 | 更新 `build_decision.c` 阈值常量：`#define B1_MAX_BUCKET_THRESHOLD 2000` | P1 | ✅ |
| PH2-02 | D-079 | 确认 `search_b1.c` 热路径采用三指针签名（`vals, lo16, dir, n, target`） | P1 | ✅ |
| PH2-03 | D-080 | 在 `b1_snapshot_t` 中预留 `uint32_t weighted_avg` 字段（暂不参与决策） | P2 | ✅ |

---

## 会议收尾

| 编号 | 决议 | 行动 | 优先级 | 状态 |
|:--:|:--:|------|:------:|:--:|
| CLOSE-01 | — | 更新 `docs/meetings/meeting_index.md` 注册 meeting_010 | P1 | ✅ |
| CLOSE-02 | — | meeting_010 所有文档最终状态同步 | P1 | ✅ |

---

> 状态标记：⬜ PENDING / 🟡 RUNNING / ✅ DONE / ❌ FAILED / 🚫 BLOCKED
>
> P0 = 签收硬性门控（不完成不签收，不启动 Phase 2）
> P1 = 与 Phase 2 实施并行或紧随其后
> P2 = 可选/标注 [DEBT]
