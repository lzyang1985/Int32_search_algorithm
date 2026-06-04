---
title: 待办清单 — Phase 2 A+B1 双路径
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Auditor
task_id: task_003_phase2_ab1
parent_doc: "docs/tasks/task_003_phase2_ab1/FINAL_task_003_phase2_ab1.md"
parent_task: task_002_phase15_cow
tags: [todo, phase2, phase3, backlog]
---

# 待办清单 — Phase 2 A+B1 双路径

> 可回流至开发阶段 (Execute) 或 Phase 3 规划。

---

## 🔴 测试类（需人工/Linux 环境）

| 编号 | 内容 | 类型 | 优先级 | 说明 |
|------|------|------|--------|------|
| TODO-01 | Linux 环境 ThreadSanitizer 零告警验证 | 测试 | ~~P1~~ ✅ | 已完成。Intel Xeon Gold 6152, Ubuntu 22.04, GCC 11.4。`make test-thread` 8/8 + `make test-thread-b1` 7/7 TSan 零告警。 |
| TODO-02 | Linux 环境 B1 benchmark 性能验证 | 性能 | P1 | Benchmark 框架已就绪编译运行。需专门 B1 路径 cycle 级性能对比（B1 vs A vs Scalar）。 |
| TODO-03 | Linux 环境 `make test-force-avx2` B1 路径验证 | 测试 | ~~P2~~ ✅ | 已完成。Force-AVX2 回归全部 PASS。 |

---

## 🟡 加固类（Phase 3 可考虑）

| 编号 | 内容 | 类型 | 优先级 | 说明 |
|------|------|------|--------|------|
| TODO-04 | SEC-01: B1→A 路径切换竞态窗口消除 | 修复 | P1 | `impl->path` 改为 `_Atomic int` 或使用 seq-lock 模式。当前 x86 上窗口极窄且 `search_b1_find` NULL 检查防崩溃，仅为正确性改善。 |
| TODO-05 | SEC-02: 弱内存模型三指针原子交换加固 | 修复 | P2 | 若需要 ARM/RISC-V 支持，使用 `_Atomic b1_snapshot_t*` struct 级 CAS。当前代码仅在 x86（TSO）上保证正确。 |

---

## 🔵 扩展类（Phase 3 规划）

| 编号 | 内容 | 类型 | 优先级 | 说明 |
|------|------|------|--------|------|
| TODO-06 | FR-05 `int32_search_find_range` 实现 | 扩展 | ✅ P2 | ✅ **Phase 3 已完成**：find_range 实现 + 100 万交叉验证 PASS。 |
| TODO-07 | FR-06 布隆过滤器 (`#ifdef USE_BLOOM_FILTER`) | 扩展 | ✅ P2 | ✅ **Phase 3 已完成**：Bloom filter 实现，假阳性率 ≤ 1%，错误否定 0。 |
| TODO-08 | SSE2 编译版本 | 扩展 | P3 | `search_avx2.c` 已有 `#ifdef INT32_SEARCH_SSE2` 框架（D-087 P3 降级） |
| TODO-09 | AVX-512 编译版本 | 扩展 | P3 | `search_avx2.c` 已有 `#ifdef INT32_SEARCH_AVX512` 框架（D-087 P3 降级） |
| TODO-10 | Windows 平台移植 | 配置 | P2 | Phase 3 部分完成。`platform_thread_yield()` 已实现（`_mm_pause` + `Sleep(0)`）。MinGW 编译命令已补充 bloom/range 测试。 |
| TODO-11 | `int32_search_config_t` 实际使用 | 扩展 | ✅ P2 | ✅ **Phase 3 已完成**：`config_t.use_bloom` 已集成到 create()；已移除 `(void)cfg`。 |

---

## ⚪ 配置类

| 编号 | 内容 | 类型 | 优先级 | 说明 |
|------|------|------|--------|------|
| TODO-12 | `INT32_SEARCH_AVX2_MIN_N` 阈值策略回顾 | 配置 | P1 | 当前值为 `SIZE_MAX`（实质禁用自动 AVX2），需要 `-DINT32SEARCH_FORCE_AVX2` 显式启用。Phase 1 遗留决策，Phase 3 可重新评估。 |
| TODO-13 | 清理 POC 归档文件 | 配置 | P3 | `src/poc_*.c` / `src/verify_*.c` 在根目录冗余。可归档到 `archive/poc/`。 |

---

## 📊 统计

| 类型 | 数量 |
|------|------|
| 测试 | 3 |
| 修复 | 2 |
| 扩展 | 6 |
| 配置 | 2 |
| **总计** | **13** |

---

> **本次审计结束，无更多自动处理。** 以上待办可直接回流给开发阶段 (Execute) 使用。
