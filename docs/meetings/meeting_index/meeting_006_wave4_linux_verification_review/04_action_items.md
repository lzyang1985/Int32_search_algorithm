---
title: 行动项 — 第四波 Linux 验证结果评审
meeting_id: meeting_006_wave4_linux_verification_review
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_006_wave4_linux_verification_review/meeting_README.md
parent_task: task_001_phase1_mvp
---

# 行动项 — 第四波 Linux 验证结果评审

---

## P0（Phase 1 收尾阻塞项，完成后方可 Frozen）

| 编号 | 行动 | 执行人 | 关联决议 | 预计耗时 |
|:---:|------|--------|:---:|:---:|
| ACT-01 | ACCEPTANCE SR-05 状态更新为「✅ Linux 验证通过」 | Agent_Auditor | D-041 | 5 min |
| ACT-02 | FINAL §3 增加性能数据来源标注（POC v3 vs Phase 1）+ VERIFY-04 引用 | Agent_Auditor | D-042 | 15 min |
| ACT-03 | FINAL §9 增加「性能数据来源差异」风险项 | Agent_Auditor | D-048 | 5 min |
| ACT-04 | DOC-01: DESIGN §2.3.2 伪代码修正 `_mm256_cmpgt_epi32(vec, key)` | Agent_Architect | D-044 | 10 min |
| ACT-05 | DOC-02: DESIGN §2.4.3 标注 CPU 检测回退实现 | Agent_Architect | D-044 | 5 min |
| ACT-06 | DOC-03: 确认/补充 README.txt MinGW 命令 | Agent_Executor | D-044 | 10 min |
| ACT-07 | DOC-04: search_avx2.c `_mm256_movemask_ps` 行增加注释 | Agent_Executor | D-044 | 2 min |
| ACT-08 | Benchmark 编译 warning 修复 | Agent_Executor | D-051 | 5 min |
| ACT-09 | README.txt Makefile 引用修正（确认存在或删除 make 命令） | Agent_Executor | D-050 | 5 min |
| ACT-10 | `include/int32_search.h` 增加 API 文档注释 | Agent_Executor | D-050 | 20 min |
| ACT-11 | Fuzz 测试实现（libFuzzer + ASan, ~60s 运行） | Agent_Executor | D-049 | 30 min |
| ACT-12 | 10M AVX2 阈值调整为 `SIZE_MAX`（或等效值） | Agent_Executor | D-047 | 2 min |
| ACT-13 | TODO 文档闭合已完成项 + 新增 AVX2 性能回归跟踪 + DEEP-05 + fuzz | Agent_Auditor | D-043 | 10 min |
| ACT-14 | FIXPLAN 文档更新：第一波→已完成、第五波→已取消、新增 DEEP-05 | Agent_Executor | D-043 | 5 min |

---

## P1（Phase 2 启动前提，不阻塞 Phase 1 Frozen）

| 编号 | 行动 | 执行人 | 关联决议 | 预计耗时 |
|:---:|------|--------|:---:|:---:|
| ACT-15 | Phase 2 DESIGN 中纳入 SEC-P2-01~10 安全强制门控 | Agent_Architect | D-048 | 30 min |
| ACT-16 | Phase 2 TASK 中原子化 SEC-P2-01~10 为独立安全任务 | Agent_Architect | D-048 | 20 min |
| ACT-17 | DEEP-05: POC v3 vs Phase 1 算法差异性能影响分析 | Agent_Algorithm | D-045 | 0.5 天 |
| ACT-18 | Path B1 微型 POC（1 天内验证 `_mm256_movemask_epi8` 路径） | Agent_Algorithm | D-046 | 1 天 |

---

## P2（跟踪项，不阻塞任何门控）

| 编号 | 行动 | 执行人 | 关联决议 | 预计耗时 |
|:---:|------|--------|:---:|:---:|
| ACT-19 | README.txt 追加 INT32SEARCH_BENCH_SEED 用法说明 | Agent_Executor | D-050 | 3 min |
| ACT-20 | README.txt 追加 10 行使用示例（create→find→destroy） | Agent_Executor | D-050 | 10 min |
| ACT-21 | D-04 double-destroy API 层 canary 防御（Phase 2 中期） | Agent_Backend | — | Phase 2 |
| ACT-22 | CMake target 级 include 控制（Phase 2） | Agent_Backend | — | Phase 2 |
| ACT-23 | Safe Probe (`__builtin_cpu_supports` 假阳性防御) Phase 2 前实施 | Agent_Security | — | Phase 2 |

---

## 已确认完成（无需行动）

| 编号 | 原待办 | 确认依据 |
|:---:|------|----------|
| ✅ | FIX-02（溢出检查） | [build_sorted.c:L30](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/build_sorted.c#L30) 已实装 |
| ✅ | FIX-03（NULL 守卫） | [platform_memory.c:L11](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/platform_memory.c#L11) 已实装 |
| ✅ | FIX-01（统一错误码） | ACCEPTANCE_FIXPLAN_wave1 记录已完成 |
| ✅ | FIX-04（double-destroy 测试） | ACCEPTANCE_FIXPLAN_wave1 记录已完成 |
| ✅ | FIX-05（benchmark 种子） | ACCEPTANCE_FIXPLAN_wave1 记录已完成 |
| ✅ | TODO-01（ASan/UBSan） | VERIFY-01 零告警 |
| ✅ | TODO-02（Valgrind） | VERIFY-02 零泄漏 |
| ✅ | TODO-03（GCC 多版本） | VERIFY-03 全兼容 |
| ✅ | DEEP-04（cpu_supports 评估） | 评估报告 Frozen |

---

## 取消项

| 编号 | 原计划 | 决议 |
|:---:|------|:---:|
| ❌ | DEEP-01（反汇编跨平台对比） | D-045 取消 |
| ❌ | DEEP-02（跨编译器对比 GCC/Clang/MSVC） | D-045 取消 |
| ❌ | DEEP-03（WSL2 vs 裸机） | D-045 取消 |

---

## 工作量汇总

| 优先级 | 项目数 | 总耗时 |
|:---:|:---:|:---:|
| P0（Phase 1 收尾） | 14 | ~1.5 小时 |
| P1（Phase 2 启动前提） | 4 | ~2 天 |
| P2（跟踪项） | 5 | Phase 2 内部 |
| **合计** | **23** | — |

---

> **所有行动项已分配。**
> P0 项由 Agent_Executor / Agent_Auditor 立即执行。
> P1 项在 Phase 2 启动时由对应 Agent 负责。
> 请人工确认决议后启动执行。
