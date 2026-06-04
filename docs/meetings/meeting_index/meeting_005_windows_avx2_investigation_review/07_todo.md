---
title: 待办清单 — meeting_005 审计偏差修复
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Human
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/06_audit_report.md"
source_docs:
  - "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
  - "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/05_completion_report.md"
trace_code:
  - "Makefile"
  - "test/test_scalar_fallback.c"
  - "src/internal.h"
tags: [todo, audit, meeting-005, deviation-fix, security]
reviewer: Human
---

# 待办清单 — meeting_005 审计偏差修复

> 此清单可直接回流至 Execute 阶段。按优先级排序，每个条目标注任务类型、严重程度和关联发现。

## 高优先级（Phase 1 收尾前修复）

| 编号 | 类型 | 严重程度 | 关联发现 | 描述 | 建议执行人 |
|------|------|----------|----------|------|-----------|
| **FIX-01** | 构建配置 | HIGH | 发现 #1 | `test-force-avx2` Makefile 目标修复：当前 `-DINT32SEARCH_FORCE_AVX2` 只传给测试源文件，静态库中的 `api.o` 未携带该宏，导致小数据集 AVX2 路径测试实际不触发。修复方案：在 `test-force-avx2` 目标中额外编译一个携带 `-DINT32SEARCH_FORCE_AVX2` 的 `api_force_avx2.o`，或增加 `force-avx2-lib` 依赖目标先重建库 | Agent_Executor |

> **FIX-01 详细说明**：
> - **缺陷**：[Makefile:L78-L83](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/Makefile#L78-L83) 的 `test-force-avx2` 依赖 `$(LIB_NAME).a`（`lib` 目标产出的普通库）
> - **关键守卫**：[api.c:L46](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L46) `#ifdef INT32SEARCH_FORCE_AVX2` 在 `api.o` 中，编译时未定义该宏
> - **影响**：SEC-03"AVX2 代码腐烂风险"的 CI 缓解措施完全失效
> - **参考实现方向**：新增 `force-avx2-lib` 目标用 `-DINT32SEARCH_FORCE_AVX2` 单独编译 `api.o` 后打包为独立的 `libint32search_force_avx2.a`，`test-force-avx2` 改为依赖该库

## 中优先级（Phase 1 收尾或 Phase 2 前修复）

| 编号 | 类型 | 严重程度 | 关联发现 | 描述 | 建议执行人 |
|------|------|----------|----------|------|-----------|
| **FIX-02** | 测试配置 | MINOR | 发现 #3 | `test_scalar_fallback` 编译命令添加 `-fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG`，与其他测试目标保持一致 | Agent_Executor |

> **FIX-02 详细说明**：[Makefile:L72-L73](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/Makefile#L72-L73) 当前缺少 sanitizer 标志。修复为一行改动：在 `$(CC) $(CFLAGS)` 后插入 `-fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG`。

## 低优先级（Phase 2 同步清理）

| 编号 | 类型 | 严重程度 | 关联发现 | 描述 | 建议执行人 |
|------|------|----------|----------|------|-----------|
| **DEBT-01** | 代码清理 | LOW | 发现 #2 | 清理 `path` 字段 + `PATH_A`/`PATH_B1` 宏死代码。建议 Phase 2 B1 路径就绪后执行（届时 `path` 字段可能被复用，清理时机视 B1 设计而定） | Agent_Executor |

> **DEBT-01 详细说明**：[internal.h:L8-L9](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L8-L9) `PATH_A`/`PATH_B1` 宏 + [L15](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L15) `path` 字段 + [api.c:L44](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L44) 写入，三者均死代码。`PATH_A`/`PATH_B1` 宏在 `poc_benchmark_v3.c` 仍有引用，不影响主线功能。

---

## 任务统计

| 类型 | 数量 |
|------|------|
| 构建配置 | 1 |
| 测试配置 | 1 |
| 代码清理 | 1 |
| **合计** | **3** |

| 严重程度 | 数量 |
|----------|------|
| HIGH | 1 |
| MINOR | 1 |
| LOW | 1 |

---

## SEC 跟踪回流

| SEC | 原状态 | 审计后状态 | 回流 TODO |
|-----|--------|----------|-----------|
| SEC-01 | ✅ 已关闭 | ✅ 保持不变 | — |
| SEC-02 | ✅ 已关闭 | ⚠️ 重新打开（测试缺 sanitizer） | **FIX-02** |
| SEC-03 | ✅ 已关闭 | ⚠️ 重新打开（CI 缓解失效） | **FIX-01** |
| SEC-04 | ✅ 已关闭 | ✅ 保持不变 | — |

> SEC-02 和 SEC-03 因审计发现偏差需重新打开。修复 FIX-01 和 FIX-02 后可再次关闭。

---

## 回流指令

- **FIX-01** / **FIX-02**：标注为 `Agent_Executor`，可直接由 Execute 阶段处理
- **DEBT-01**：需 Phase 2 Architect 评估后决定清理时机，不阻塞当前交付
- 修复 FIX-01 + FIX-02 后，meeting_005 可进入 Frozen

> **本次审计结束，无更多自动处理。**
> 会议 meeting_005 的 12 项 TODO + 4 项 SEC 实质性工作均已完成。
> 本清单 3 项待办用于修复审计偏差（1 HIGH / 1 MINOR / 1 LOW）。
