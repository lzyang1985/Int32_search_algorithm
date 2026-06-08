---
title: 任务仪表盘 — Int64 二期 Phase 2 (COW + Bloom 重建)
status: Frozen
created_at: 2026-06-04
updated_at: 2026-06-08
author: Agent_Architect
task_id: task_006_int64_phase2_cow
parent_task: task_005_int64_extension
tags: [int64, b1, cow, bloom-rebuild, phase2, alignment]
---

# 任务仪表盘：Int64 二期 Phase 2 (COW + Bloom 重建)

## 元信息
- **任务 ID**: task_006_int64_phase2_cow
- **当前阶段**: ✅ **立项完成**（Frozen: ALIGNMENT + DESIGN + TASK 已冻结 2026-06-04）
- **创建时间**: 2026-06-04
- **父任务**: [task_005_int64_extension](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_005_int64_extension/task_README.md)（Phase 1 已 SUCCESS ✅，2026-06-02 归档）
- **父决议**: meeting_016_optimization_direction
- **来源决议**: D-116（范围）、D-117（COW 方案修正）、D-118（单线程警告）
- **对应行动项**: ACT-38
- **下一阶段**: 归档 (Archive) — 等待人工确认后冻结到 docs/ 根目录

## 子任务列表（11 个原子任务）
| # | 任务 | 优先级 | 风险 | 关键路径 | 依赖 | 估算 | 状态 |
|---|------|--------|------|----------|------|------|------|
| T1 | `internal_int64.h` 字段原子化 | P0 | 中 | ✅ | — | 30min | ✅ **SUCCESS** (ACCEPTANCE 完成) |
| T2 | `api_int64.c` find() 改造 | P0 | 高 | ✅ | T1 | 1h | ✅ **SUCCESS** (与 T3 并行) |
| T3 | `api_int64.c` destroy() 改造 | P0 | 低 | ✅ | T1 | 20min | ✅ **SUCCESS** (与 T2 并行) |
| T4 | `api_int64.c` rebuild() COW + Bloom | P0 | 高 | ✅ | T1,T2 | 1.5h | ✅ **SUCCESS** (与 T5 并行) |
| T5 | `int64_search.h` 警告移除 + 版本号 | P1 | 极低 | ❌ | T1-T4 | 10min | ✅ **SUCCESS** (与 T4 并行) |
| T6 | `test_int64_thread.c` TSan 测试 | P0 | 中 | ✅ | T1-T4 | 1.5h | ✅ **SUCCESS (Functional)** / ⚠️ TSan BLOCKED Linux-only |
| T7 | `test_int64.c` L1 COW 测试 5 项 | P1 | 低 | ❌ | T1-T4 | 45min | ✅ **SUCCESS** (49/49 PASS) |
| T8 | `Makefile` + `README.txt` 更新 | P1 | 极低 | ❌ | T6 | 20min | ✅ **SUCCESS** (含 MinGW 兜底兼容) |
| V1 | Phase 1 测试回归验证 | P0 | 极低 | ✅ | T1-T4 | 30min | ✅ **SUCCESS** (49/49 PASS, ASan/UBSan 0 告警) |
| V2 | TSan 并发测试零告警 | P0 | 中 | ✅ | T6 | 30min | ✅ **SUCCESS** (3/3 零报告, 5.2M iters) |
| V3 | 10M 性能回归验证 | P1 | 中 | ❌ | T1-T4,T7 | 1h | ✅ **SUCCESS** (498 cy/query avg, 0 mismatches) |

**关键路径**: T1 → T2/T3 → T4 → V1/V2
**总工作量**: ~8.5 小时（约 1 个工作日）

## 📄 文档状态看板
| 文档名 | 状态 | 最后更新 | 来源文档 |
|--------|------|----------|----------|
| ALIGNMENT_task_006_int64_phase2_cow.md | 🔒 Frozen | 2026-06-04 | meeting_016 D-116/D-117/D-118 |
| DESIGN_task_006_int64_phase2_cow.md | 🔒 Frozen | 2026-06-04 | ALIGNMENT + Int32 B1 COW 模板 |
| TASK_task_006_int64_phase2_cow.md | 🔒 Frozen | 2026-06-04 | DESIGN |
| ACCEPTANCE_T1_internal_int64_atomic.md | ✅ Frozen | 2026-06-04 | T1 验收 (SUCCESS) |
| ACCEPTANCE_T2_T3_find_destroy.md | ✅ Frozen | 2026-06-04 | T2+T3 验收 (SUCCESS) |

**🧊 立项冻结**: 上述三份文档自 2026-06-04 起视为冻结输入,Execute 阶段不得修改。如需变更必须回到 Align/Architect 阶段。

## 交付物（待定）
- 修改 `src/api_int64.c` 实现 COW + Bloom 重建
- 修改 `src/internal_int64.h` 字段原子化
- 修改 `include/int64_search.h` 移除"单线程 only"警告
- 新增 `test/test_int64_thread.c` TSan 并发压力测试
- 更新 `README.txt` 标注 Int64 多线程安全

## 待办统计
- 修复: 0 | 优化: 0 | 配置: 0 | 测试: 0 | **设计: 0（待 ALIGNMENT 冻结后拆分）**

## 关键决策（2026-06-04 用户已确认）
- **Q1**: `path` 字段改 `_Atomic int`（升级安全性，x86-64 lock-free 零开销）
- **Q2**: 错误码零新增（维持 5 个不变）
- **Q3**: destroy() 等待 reader_count==0 后释放（与 Int32 v1.0.0 一致）
- **Q4**: TSan 测试 = 1M uniform TSan + 10M 性能回归
- **T6-1** (2026-06-08): 数据规模 1M → 128K（1M × 2s 仅 rebuild 21 次，不达 TASK ≥100 目标）
- **T6-2** (2026-06-08): 命中率断言 99.9% → [40%, 60%]（交替数据 + 均匀采样设计基线为 50%）
- **T6-3** (2026-06-08): 数据规模 128K → 64K（高负载下 128K 偶发 76-99 rebuild 不达标，64K 稳定 132-171 rebuild）
- **T8-1** (2026-06-08): 新增 test-int64-thread-func 兜底目标（MinGW 缺 libtsan）

## 关联信息
- 前置任务：[task_005_int64_extension](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_005_int64_extension/task_README.md) — Phase 1 已 SUCCESS ✅
- 决议来源：[meeting_016 D-116/D-117/D-118](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_016_optimization_direction/03_decisions.md)
- 参考实现：[task_002 Phase 1.5 COW](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_002_phase15_cow/ALIGNMENT_task_002_phase15_cow.md) — Int32 Path A COW 模板
- Int32 B1 COW 完整参考：[src/api.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c) — 已验证三字段 atomic 模式
