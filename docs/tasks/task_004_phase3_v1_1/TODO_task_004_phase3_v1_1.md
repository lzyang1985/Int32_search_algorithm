---
title: 待办清单 — Phase 3 v1.1 扩展与跨平台
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Auditor
task_id: task_004_phase3_v1_1
parent_doc: "docs/tasks/task_004_phase3_v1_1/FINAL_task_004_phase3_v1_1.md"
parent_task: root
tags: [todo, phase3, backlog]
---

# 待办清单 — Phase 3 v1.1 扩展与跨平台

> 可回流至开发阶段 (Execute) 或后续 Phase 规划。

---

## 🔴 测试类（需 Linux 环境）

| 编号 | 内容 | 类型 | 优先级 | 说明 |
|------|------|------|--------|------|
| TODO-01 | Linux 环境 TSan 回归验证 | 测试 | ✅ | **已完成**。Intel Xeon Gold 6152, GCC 11.4, Ubuntu 22.04。`make test-thread` 8/8 + `make test-thread-b1` 7/7 TSan 零数据竞争。 |
| TODO-02 | Linux 环境 ASan/UBSan 全量测试 | 测试 | ✅ | **已完成**。`make test + test-b1 + test-range + test-bloom` 全部 ASan/UBSan 零告警 PASS。 |
| TODO-03 | 10M 规模布隆假阳性率实测 | 测试 | ✅ | **已完成**。10M 插入 + 1M 查询，假阳性 0.000% (0/1,000,000)，远低于 1% 目标。 |

---

## 🟡 加固类

| 编号 | 内容 | 类型 | 优先级 | 说明 |
|------|------|------|--------|------|
| TODO-04 | SEC-04: search_scalar_lower_bound/upper_bound 增加 NULL vals 检查 | 加固 | Low | 与 `search_scalar_find` 保持一致的防御风格。当前调用链安全（内部函数，vals 不可能 NULL），改善代码一致性。 |
| TODO-05 | SEC-01: rebuild() 支持动态 bloom 配置变更 | 增强 | P3 | 当前 `rebuild()` 不接收 `cfg` 参数，无法在重建时改变 bloom 状态。如需支持，需修改 `rebuild()` 接口签名。 |

---

## 🔵 扩展类（后续 Phase）

| 编号 | 内容 | 类型 | 优先级 | 说明 |
|------|------|------|--------|------|
| TODO-06 | SSE2 编译版本 | 扩展 | P3 | D-087 降级。`search_avx2.c` 已有 `#ifdef INT32_SEARCH_SSE2` 框架，需实现 4 路 SIMD 二分。 |
| TODO-07 | AVX-512 编译版本 | 扩展 | P3 | D-087 降级。`search_avx2.c` 已有 `#ifdef INT32_SEARCH_AVX512` 框架，需实现 16 路 SIMD 二分。 |
| TODO-08 | MSVC 编译器支持 | 扩展 | P3 | 当前仅 MinGW。MSVC 需 `__cpuidex` + `_InterlockedExchange` 等 MSVC 特有 API 封装。 |
| TODO-09 | MinGW AVX2 性能退化调查与修复 | 扩展 | P3 | meeting_005 确认退化 0.45x-0.55x。可能原因：MinGW `_mm256_loadu_si256` 代码生成策略。 |

---

## ⚪ 配置/清理类

| 编号 | 内容 | 类型 | 优先级 | 说明 |
|------|------|------|--------|------|
| TODO-10 | 清理 POC 归档文件 | 配置 | P3 | `src/poc_*.c` / `src/verify_*.c` 可归档到 `archive/poc/`，减少 src/ 目录噪音。 |
| TODO-11 | SEC-03: xxHash 编译单元管理 | 配置 | P3 | 若后续更多模块使用 xxHash，建议提取 `XXH_IMPLEMENTATION` 到独立的 `xxhash_impl.c`，避免多翻译单元定义冲突。 |

---

## 📊 统计

| 类型 | 数量 |
|------|------|
| 测试 | 0（3/3 已完成） |
| 加固 | 2 |
| 扩展 | 4 |
| 配置 | 2 |
| **总计** | **8**（3 项已完成） |

---

## ✅ 已完成的 Phase 3 任务（记录）

| 编号 | 内容 | 状态 |
|------|------|------|
| T1 | Windows yield + ACT-03 | ✅ |
| T2 | 文档更新 ACT-04,06,07 | ✅ |
| T3 | 注释补充 ACT-05 | ✅ |
| T4 | find_range 实现 | ✅ |
| T5 | 布隆过滤器 | ✅ |
| T6 | 安全加固 ACT-08,09,10 | ✅ |
| T7 | 构建系统更新 | ✅ |
| T8 | 集成验证 | ✅ |

---

> **本次审计结束，无更多自动处理。** 以上待办可直接回流给开发阶段 (Execute) 使用。
