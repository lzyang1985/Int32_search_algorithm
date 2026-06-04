---
title: 待办清单 — Phase 1 MVP (Path A 单路径)
status: Frozen
created_at: 2026-05-29
updated_at: 2026-06-01
author: Agent_Auditor
task_id: task_001_phase1_mvp
parent_doc: "docs/tasks/task_001_phase1_mvp/ACCEPTANCE_task_001_phase1_mvp.md"
parent_task: task_001_phase1_mvp
source_meeting: meeting_004_phase1_audit_review, meeting_006_wave4_linux_verification_review, meeting_007_poc_strategy
reviewer: Human
tags: [todo, phase1, mvp, path-a, audited]
---

# 待办清单 — Phase 1 MVP (Path A 单路径)

> 此清单可直接回流至 Execute 阶段。按优先级排序，每个条目标注任务类型、严重程度和关联偏差。

## 高优先级 (Phase 1 收尾)

| 编号 | 类型 | 严重程度 | 关联偏差 | 描述 | 建议执行人 | 状态 |
|------|------|----------|----------|------|-----------|------|
| TODO-01 | 验证 | Medium | — | Linux 服务器上 `gcc -O3 -std=c11 -mavx2 -fsanitize=address,undefined` 编译全项目并运行测试，确认零告警 | 人工 (Linux) | ✅ VERIFY-01 (零告警) |
| TODO-02 | 验证 | Medium | — | Linux 服务器上 `valgrind --leak-check=full ./int32search_test` 内存泄漏检测 | 人工 (Linux) | ✅ VERIFY-02 (零泄漏) |
| TODO-07 | 测试 | Medium | D-04 | 新增 `test_double_destroy` 测试用例 | Agent_Executor | ✅ FIX-04 (已添加) |

## 中优先级 (Phase 2 前修复)

| 编号 | 类型 | 严重程度 | 关联偏差 | 描述 | 建议执行人 | 状态 |
|------|------|----------|----------|------|-----------|------|
| TODO-05 | 修复 | Low | D-05 | 在 `internal.h` 中 `#include "../include/int32_search.h"` 统一错误码定义，删除 `search_scalar.c` 和 `search_avx2.c` 中的重复 `#define` | Agent_Executor | ✅ FIX-01 (已修复) |
| TODO-03 | 验证 | Low | — | 在不同 GCC 版本 (8/9/10/11) 上编译验证（CI/CD 可自动化） | 人工/CI | ✅ VERIFY-03 (9/10/11/12 全通过) |

## 低优先级 (Phase 2/3 同步)

| 编号 | 类型 | 严重程度 | 关联偏差 | 描述 | 建议执行人 | 状态 |
|------|------|----------|----------|------|-----------|------|
| TODO-04 | 性能 | Low | — | Linux 服务器上 Xeon Gold 6226 跑完整 benchmark 获取官方性能数据 | 人工 (Linux) | ✅ VERIFY-04 (Xeon Gold 6152, 1M/5M/10M) |
| TODO-06 | 文档 | Low | D-02 | 更新 [DESIGN_task_001_phase1_mvp.md](DESIGN_task_001_phase1_mvp.md) 2.3.2 节伪代码 | Agent_Architect | ✅ DOC-01 (已修正) |
| TODO-09 | 文档 | Low | D-03 | 更新 [DESIGN_task_001_phase1_mvp.md](DESIGN_task_001_phase1_mvp.md) 2.4.3 节：CPU 检测回退标注 | Agent_Architect | ✅ DOC-02 (已标注) |
| TODO-08 | 文档 | Low | D-06 | 在 [README.txt](../../../README.txt) 中增加 Windows MinGW 环境下 gcc 逐条编译命令 | Agent_Executor | ✅ DOC-03 (已补充) |

## Windows AVX2 异常调查 (meeting_004 D-032/D-033 新增)

| 编号 | 类型 | 严重程度 | 关联决议 | 描述 | 建议执行人 | 状态 |
|------|------|----------|----------|------|-----------|------|
| TODO-10 | 调查 | High | D-032, D-033 | Windows AVX2 性能异常调查。三步全部完成：① popcount 指令验证 ✅；② 无需替换（硬件指令正常）；③ Benchmark 公平对比改造 ✅。结论：算法结构性瓶颈，非平台/编译器问题。详见 INVESTIGATION / meeting_005 / meeting_006 | 人工 + Agent_Executor | ✅ 完成 (meeting_005 Frozen) |

## 安全改进 (meeting_004 Code Security Expert 新增)

| 编号 | 类型 | 严重程度 | 关联决议 | 描述 | 建议执行人 | 状态 |
|------|------|----------|----------|------|-----------|------|
| S-TODO-01 | 修复 | Medium | D-029 | `build_sort_and_validate` 增加 `n > SIZE_MAX / sizeof(int32_t)` 溢出检查（32 位平台有 `malloc(0)` 风险） | Agent_Executor | ✅ FIX-02 (已实装) |
| S-TODO-02 | 修复 | Low | D-029 | `platform_aligned_free` 增加显式 `if (ptr != NULL)` 守卫（`_mm_free(NULL)` 行为实现定义，某些平台可能崩溃） | Agent_Executor | ✅ FIX-03 (已实装) |
| S-TODO-03 | 评估 | Medium | D-029 | `__builtin_cpu_supports("avx2")` 假阳性风险评估：虚拟机/模拟器场景可能返回 true 但不支持 AVX2 指令，触发 #UD 崩溃。需评估防御策略（try/catch SEH 或启动时 safe probe） | Agent_Architect | ✅ 已完成（[评估报告](ASSESSMENT_cpu_supports_false_positive_task_001_phase1_mvp.md)，结论：Low-Medium 风险，推荐 Phase 2 前实施 Safe Probe） |
| S-TODO-04 | 工程 | Low | D-029 | Benchmark `srand(time(NULL))` 不可重现，支持 `INT32SEARCH_BENCH_SEED` 环境变量 | Agent_Executor | ✅ FIX-05 (已实装) |
| S-TODO-05 | 文档 | Low | D-029 | `search_avx2.c` 中 `_mm256_movemask_ps(_mm256_castsi256_ps(cmp))` 增加 "Intel 标准惯用法，Haswell+ 无跨域惩罚" 注释 | Agent_Executor | ✅ DOC-04 (已添加) |

## 任务统计

| 类型 | 原数量 | 已完成 |
|------|:------:|:------:|
| 修复 | 3 | 3 ✅ |
| 测试 | 1 | 1 ✅ |
| 验证 | 3 | 3 ✅ |
| 性能 | 1 | 1 ✅ |
| 文档 | 4 | 4 ✅ |
| 调查 | 1 | 1 ✅ |
| 评估 | 1 | 1 ✅ |
| 工程 | 1 | 1 ✅ |
| **合计** | **15** | **15 ✅** |

## Phase 1 收尾残余 (meeting_006 P0 全部闭合)

| 编号 | 类型 | 来源 | 描述 | 执行人 | 状态 |
|------|------|------|------|--------|:--:|
| P0-ACT-01 | 文档 | D-041 | ACCEPTANCE SR-05 → "✅ Linux 验证通过" | Agent_Auditor | ✅ (Phase 2 task_003 ASan 全量通过) |
| P0-ACT-02 | 文档 | D-042 | FINAL §3 增加 POC v3 vs Phase 1 性能数据来源标注 | Agent_Auditor | ✅ |
| P0-ACT-03 | 文档 | D-048 | FINAL §9 增加 "性能数据来源差异" 风险项 | Agent_Auditor | ✅ |
| P0-ACT-08 | 代码 | D-051 | Benchmark 编译 unused-variable warning 修复 | Agent_Executor | ✅ (`-Wall -Wextra` 零警告) |
| P0-ACT-09 | 文档 | D-050 | README.txt Makefile 引用修正 | Agent_Executor | ✅ (已含 Windows+Linux 双套命令) |
| P0-ACT-10 | 文档 | D-050 | include/int32_search.h 增加 API 文档注释 | Agent_Executor | ✅ (Doxygen 注释完整) |
| P0-ACT-11 | 测试 | D-049 | Fuzz 测试实现 (libFuzzer + ASan) | Agent_Executor | ✅ (`test/test_fuzz.c` 已存在) |
| P0-ACT-12 | 代码 | D-047 | 10M AVX2 阈值调整为 SIZE_MAX | Agent_Executor | ✅ (`internal.h:12: #define INT32_SEARCH_AVX2_MIN_N SIZE_MAX`)

## Phase 2 POC 待办 (meeting_007)

| 编号 | 类型 | 来源 | 描述 | 执行人 | 状态 |
|------|------|------|------|--------|:--:|
| POC-01~03 | 分析 | D-053 | DEEP-05: POC v3 vs Phase 1 性能差异定量分析 | Agent_Algorithm | ⏳ |
| POC-04~07 | 开发 | D-053 | Eytzinger POC: BFS 布局 + branchless search + benchmark | Agent_Executor | ⏳ |
| POC-08~10 | 验证 | D-053 | B1 重验证: 计时修正 + benchmark + crossover 确认 | Agent_Executor | ⏳ |
| POC-11~13 | 开发 | D-053 | S-tree POC: B-tree + SIMD (条件触发: Eytzinger < 2x) | Agent_Executor | ⏳ |

## 回流指令

P0 残余 8 项在 meeting_006 Frozen 后由 Agent_Executor / Agent_Auditor 执行。
POC 13 项在 meeting_007 Frozen 且 P0 收尾完成后启动。

> **Phase 1 原始 15 项 TODO 全部闭合。meeting_006/007 新增 8+13 = 21 项待办待执行。**
