---
title: 行动项 — 剩余待办事项收尾路线确认会
meeting_id: meeting_020_todo_roadmap_confirmation
status: Reviewing
created_at: 2026-06-10
updated_at: 2026-06-10
author: Agent_Executor
parent_doc: docs/meetings/meeting_index.md
source_doc: "03_decisions.md"
---

# 行动项

## Phase A' 行动项（P0/P1，收尾必做）

| # | 行动项 | 优先级 | 负责 | 工作量 | 依赖 | 验收标准 |
|---|--------|--------|------|--------|------|----------|
| ACT-60 | Int64 B1 D-143 防御移植：`search_b1_int64.c` 追加 `if ((size_t)end > n) return (size_t)-1;` | **P0** | Sec | 30min | 无 | SG-2 闭合，编译通过 |
| ACT-61 | Int64 B1 D-142 小桶标量路径移植（阈值=4），条件编译 `#ifdef INT64_SEARCH_B1_SMALL_BUCKET` | P1 | Algo | 30min | 无 | 编译通过，bench 无退化 |
| ACT-62 | Dir fuzz 基础覆盖率：注入 start/end 异常值对，Int32+Int64 双覆盖 | P1 | Sec | 1天 | ACT-60 | SG-3 闭合，fuzz 零崩溃 |
| ACT-63 | bench_100.ps1 方法论增强：分位数（P50/P90/P95/P99）+ StdDev + 前5轮丢弃 + 关闭 Write-Progress | P1 | Fullstack | 1h | 无 | 重跑四场景，产出分位数报告 |
| ACT-64 | 64B 对齐独立实施：`platform_memory.c` `aligned_alloc(64, ...)` | P1 | Backend | 5min | 无 | 编译通过，bench 无退化 |
| ACT-65 | Huge Pages 快速验证：Linux `perf stat -e dTLB-load-misses` baseline → `madvise(MADV_HUGEPAGE)` → bench 对比 | P1 | Algo | 2h | Linux 环境 | 产出 TLB miss 对比 + perf 对比报告 |
| ACT-66 | PGO Makefile target：`make pgo`（instrument → bench 训练 → profile-use 重编译），产物输出到 `build/pgo/` | P1 | Backend | 1天 | 无 | bench_100 PGO vs baseline 对比 |
| ACT-67 | find_with_hint 最小原型：B1 hint 优先扫描 + `int32_search_find_with_hint()` + 时序 benchmark | P1 | Fullstack | 2天 | 无 | benchmark 随机无退化 + 局部性场景有收益，`@experimental` |
| ACT-68 | task_006 归档：生成 FINAL + TODO 文档，更新 `docs/README.md` | P1 | DocMgr | 30min | 无 | 归档记录完成 |

**Phase A' 总计：约 4.5-5.5 个工作日。** P0 项（ACT-60）应在会后人立即执行。

---

## Phase B 行动项（Gate 后条件触发）

| # | 行动项 | 优先级 | 负责 | 工作量 | 触发条件 |
|---|--------|--------|------|--------|----------|
| ACT-69 | 预取调优 Stage 0：L3 miss rate baseline | P1 | Algo | 0.5天 | Phase A' 完成 |
| ACT-70 | 预取调优 Stage 1：6组实验（3种distance × 2种hint） | P2 | Algo | 1天 | Stage 0 L3 miss ≥5% |
| ACT-71 | 热键缓存 workload 埋点 | P1 | Algo | 2天 | Phase A' 完成 |
| ACT-72 | D-140 重验证（30min时间盒） | P2 | Algo | 30min | ACT-66 PGO 完成 |
| ACT-73 | LTO Makefile opt-in target | P2 | Backend | 0.5天 | Phase A' 完成 |
| ACT-74 | DX收尾：README 完善 + CI 扩展 Int64 | P2 | Fullstack | 0.5天 | 维护模式准入前 |

---

## 已关闭行动项（不再追踪）

| 原编号 | 行动项 | 关闭理由 | 关闭标注 |
|--------|--------|----------|----------|
| U-09 | 编译器flag实验 | 合并到 U-02 PGO 工作中 | D-173 |
| U-12 | 热键缓存完整实现 | 条件：U-05 埋点收益 > 1.8x | D-173 |
| U-14 | COW seqlock调研 | 多线程需求未浮现，D-156 opt-out 已解决单线程 | `[YAGNI-Seqlock]` |
| U-15 | find_with_hint API调研 | 已被 U-08 覆盖 | D-173 |
| U-16 | 批量查询API | D-136 已明确等待业务场景 | `[YAGNI-BatchAPI]` |

---

## 维护模式门禁检查清单

| 门禁 | 内容 | 状态 | 对应行动项 |
|------|------|------|-----------|
| G1 | Int32 Feature Complete | ✅ | — |
| G2 | Int64 Phase 2 (COW+Bloom重建) | ✅ | — |
| G3 | CI/CD 基础版 | ✅ | ACT-35 已完成 |
| G4 | TODO P1 闭合 | ✅ | — |
| G5 | MinGW 退化实验有定论 | ✅ | — |
| SG-1 | ASan/UBSan/TSan 零告警 | ✅ | — |
| SG-2 | Int64 B1 D-143 等效防御 | ❌ → ACT-60 | ACT-60 |
| SG-3 | Dir fuzz 基础覆盖率 | ❌ → ACT-62 | ACT-62 |
| SG-4 | vgather 禁令文档化 | ⚠️ PARTIAL | 5min 文档 |
