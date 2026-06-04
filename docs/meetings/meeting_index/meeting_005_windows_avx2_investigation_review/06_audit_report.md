---
title: 审计报告 — meeting_005 全部 TODO/SEC 完成审查
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Human
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/meeting_README.md"
source_docs:
  - "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/01_agenda.md"
  - "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
  - "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/05_completion_report.md"
trace_code:
  - "src/internal.h"
  - "src/api.c"
  - "src/search_avx2.c"
  - "src/platform_cpu.c"
  - "Makefile"
  - "test/test_scalar_fallback.c"
tags: [audit, meeting-005, avx2, todo-review, security]
reviewer: Human
---

# 审计报告 — meeting_005 全部 TODO/SEC 完成审查

## 1. 执行摘要

| 维度 | 结论 |
|------|------|
| **总体评价** | **PASS（附条件）** — 12/12 TODO + 4/4 SEC 实质性工作均已落地 |
| 已验证通过 | 11/12 TODO + 3/4 SEC — 代码和文档变更确认正确 |
| 偏差发现 | **3 项**：1 HIGH（构建配置）/ 1 MINOR（测试配置）/ 1 LOW（死代码） |
| 安全风险 | 无新增安全漏洞；FORCE_AVX2 CI 形式生效实质失效为中等风险 |
| 接口契约 | D-038 函数指针方案 5/5 符合决议规格 |

> **审计方法**：源代码级交叉验证。对 12 项 TODO 和 4 项 SEC 的每一项目，读取对应的完成报告 → 定位被修改的源文件 → 逐行比对决议要求与实现。

---

## 2. 逐项审计结果

### 2.1 P0 — 关键路径

| TODO | 决议 | 审计结论 | 证据 |
|------|------|----------|------|
| TODO-01 | D-037 | ✅ 通过 | 编译选项对比实验报告 E1-E4 完整。E2 `-march=native` 双平台验证未恢复性能，E4 POC 修复版确认原 "3.53x" 为 bug 假象。结论：根因为算法层面效率瓶颈 |
| TODO-02 | D-038 | ✅ 通过 | [internal.h:L11](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L11) `INT32_SEARCH_AVX2_MIN_N` 宏 + [L17-L18](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L17-L18) `search_fn`/`avx2_capable` 字段 + [api.c:L45-L58](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L45-L58) create() 分派逻辑 + [api.c:L77](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L77) find() 单行间接调用 |
| TODO-03 | D-036 | ✅ 通过 | [技术路线.md §1 表格](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/architecture/技术路线.md) "Linux GCC 3.5x-5.1x … Windows MinGW 已知退化" + §7 新增风险条目 |
| TODO-04 | D-035 | ✅ 通过 | [FINAL_task_001_phase1_mvp.md §8](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/FINAL_task_001_phase1_mvp.md) 新增"已知平台限制"表格 |

### 2.2 P1 — 高优先级

| TODO | 决议 | 审计结论 | 证据 |
|------|------|----------|------|
| TODO-05 | D-034, D-040 | ✅ 通过 | INVESTIGATION 报告 §1 POC v1 对比基准 + §7.1 不确定度声明（升级为确认结论）+ §9 编译选项缺口 |
| TODO-06 | D-035 | ✅ 通过 | [platform_cpu.c:L4-L14](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/platform_cpu.c#L4-L14) 注释：本函数仅检测 CPU 硬件能力，不反映编译器代码生成质量 |
| TODO-07 | A-01 | ✅ 通过 | N=5 标准差数据集成至 TODO-01 报告 §4。AVX2 σ/μ 1.4%-4.1%，5 轮无一例外 |

### 2.3 P2 — 中优先级

| TODO | 决议 | 审计结论 | 证据 |
|------|------|----------|------|
| TODO-08 | D-039, SEC-01 | ✅ 通过 | [search_avx2.c:L30](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/search_avx2.c#L30) 防御性注释标注 while 条件依赖 |
| TODO-09 | D-039, SEC-03 | ⚠️ **偏差** | 见 §3 发现 #1：FORCE_AVX2 CI 构建无效 |
| TODO-10 | D-039, 盲区1 | ✅ 通过 | objdump 确认：零 YMM 溢出 / 3 出口 vzeroupper / 415B .text |

### 2.4 P3 — 低优先级

| TODO | 决议 | 审计结论 | 证据 |
|------|------|----------|------|
| TODO-11 | D-037 | ✅ 通过 | Linux Xeon Gold 6152 / GCC 11.4.0 benchmark：AVX2 0.45-0.50x vs 标量。确认为 `-march=native` 缺失导致退化 |
| TODO-12 | SEC-02 | ✅ 通过 | [test_scalar_fallback.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/test_scalar_fallback.c) 新增 7/7 PASS 已集成 Makefile test 目标 | ⚠️ 子偏差：缺少 sanitizer 编译标志 |

### 2.5 SEC 跟踪表

| SEC | 原严重程度 | 当前状态 | 审计确认 |
|-----|----------|----------|----------|
| SEC-01 | Medium | ✅ 注释已添加 | 缓解有效 |
| SEC-02 | Minor | ✅ 测试已新增 | 测试正确但缺 sanitizer（见发现 #3） |
| SEC-03 | Minor | ⚠️ CI 配置无效 | 缓解实质失效（见发现 #1） |
| SEC-04 | Medium | ✅ 评估报告已交付 | [ASSESSMENT_cpu_supports_false_positive_task_001_phase1_mvp.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/ASSESSMENT_cpu_supports_false_positive_task_001_phase1_mvp.md) 结论 Low-Medium 合理 |

---

## 3. 偏差清单（Deviation Log）

### 发现 #1：`test-force-avx2` 目标构建无效 [HIGH] — 构建配置偏差

**位置**：[Makefile:L78-L83](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/Makefile#L78-L83)

**描述**：`test-force-avx2` 目标将 `-DINT32SEARCH_FORCE_AVX2` 传递给测试源文件（`test_unit.c` / `test_correctness.c` / `test_boundary.c`），但 `#ifdef INT32SEARCH_FORCE_AVX2` 守卫位于 [src/api.c:L46](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L46)，该文件已预编译为静态库 `$(LIB_NAME).a` 中的 `api.o`，**未携带 `-DINT32SEARCH_FORCE_AVX2`**。

**根因链条**：
1. `test-force-avx2` 依赖 `$(LIB_NAME).a`（由 `make lib` 构建，不含 `-DINT32SEARCH_FORCE_AVX2`）
2. 链接时 `api.o` 中的 `create()` 函数永远走 `#else` 分支（`n > INT32_SEARCH_AVX2_MIN_N` 阈值检查）
3. 小数据集（n=100）永远不触发 AVX2 路径

**影响**：SEC-03"AVX2 代码腐烂风险"的缓解措施**形式上存在、实质上无效**。在小数据集 CI 场景中 AVX2 路径不会被编译执行。

**与现有文档冲突**：TODO-09 完成记录声称"n=100~n=100000，均走 AVX2"，但当前 Makefile 无法达成此效果（除非手动 `make lib` 时也传递宏）。

**严重程度**：HIGH — CI 安全控制失效。不阻塞生产代码路径（生产路径由运行时阈值控制）。

### 发现 #2：`path` 字段成为死代码 [LOW] — 代码清理偏差

**位置**：
- [internal.h:L8-L9](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L8-L9) — `PATH_A` / `PATH_B1` 宏定义
- [internal.h:L15](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L15) — `int path` 字段
- [api.c:L44](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L44) — `impl->path = PATH_A` 写入

**描述**：TODO-02/D-038 实施函数指针方案后，查询路径选择完全由 `search_fn` 决定。`path` 字段仅被写入且取值始终为 `PATH_A`，从未被读取。

**影响**：无功能影响，但增加维护混淆。`PATH_A`/`PATH_B1` 宏在 POC benchmark 代码仍使用（`poc_benchmark_v3.c`），与主线代码产生概念分裂。

**建议处理**：Phase 2 B1 路径就绪后统一清理（届时 `path` 字段可能被复用）。

**严重程度**：LOW — 死代码，无安全/功能影响。

### 发现 #3：`test_scalar_fallback` 缺少 sanitizer 编译标志 [MINOR] — 测试配置偏差

**位置**：[Makefile:L72-L73](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/Makefile#L72-L73)

**描述**：其他所有测试目标均使用 `-fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG`，但 `test_scalar_fallback` 缺少这些标志。

**当前**：
```makefile
$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_scalar_fallback.c $(LIB_NAME).a -o int32search_scalar_fallback -lm
```

**预期**（与其他测试一致）：
```makefile
$(CC) $(CFLAGS) -fsanitize=address,undefined -g -DINT32_SEARCH_DEBUG -I$(INCDIR) -I$(SRCDIR) $(TESTDIR)/test_scalar_fallback.c $(LIB_NAME).a -o int32search_scalar_fallback -lm
```

**影响**：内存错误和未定义行为无法在该测试中被 ASan/UBSan 检测。

**严重程度**：MINOR — 测试未被 sanitizer 覆盖，不影响生产代码。

---

## 4. 安全风险评估

### 4.1 内存安全

| 检查项 | 结果 | 证据 |
|--------|------|------|
| `calloc` ↔ `free` 配对 | ✅ | `int32_search_create()` L34 calloc ↔ `int32_search_destroy()` L96 free，路径完整 |
| `platform_aligned_free` 配对 | ✅ | `build_sort_and_validate()` 内部分配 ↔ `destroy()` 释放 |
| `_mm256_loadu_si256` 越界 | ✅ | `block = hi - 8` 边界检查已确认安全（while 条件保证 hi ≥ 8） |
| `vals[last_le]` 边界访问 | ✅ | 前序调查独立推导确认安全 |
| `out_index` 空指针解引用 | ✅ | `int32_search_find()` L74 防御性检查 `if (out_index == NULL)` |
| `handle` 空指针解引用 | ✅ | `int32_search_find()` L73 `if (handle == NULL)` + `destroy()` L88 同 |

### 4.2 SIMD 安全性

| 检查项 | 结果 | 证据 |
|--------|------|------|
| YMM 寄存器溢出（spill） | ✅ | TODO-10 objdump 确认零溢出，无 `vmovdqu [rsp+...]` 类指令 |
| `vzeroupper` 覆盖 | ✅ | 3/3 出口路径均覆盖（成功+out_index / 成功 / NOT_FOUND） |
| 非对齐加载 | ✅ | `vmovdqu` 非对齐加载，无 `vmovdqa` 对齐假设 |
| `_mm256_castsi256_ps` 跨域 | ✅ | Haswell+ 零延迟寄存器重命名，注释已标注 |
| 栈帧大小 | ✅ | 仅 8 字节（rbx），无栈溢出风险 |
| `.text` 段 | ✅ | 415 bytes，完全适配 L1 指令缓存（32KB） |

### 4.3 SEC 最终状态

| SEC | 描述 | 严重程度 | 审计后状态 |
|-----|------|----------|-----------|
| SEC-01 | `block = hi - 8` 脆性依赖 | Medium | ✅ 已缓解（注释已添加） |
| SEC-02 | API else 分支未覆盖 | Minor | ⚠️ 已缓解但测试缺 sanitizer（见发现 #3） |
| SEC-03 | AVX2 代码腐烂风险 | Minor | ⚠️ 缓解无效（见发现 #1） |
| SEC-04 | `__builtin_cpu_supports` 假阳性 | Medium | ✅ 已关闭（评估报告 Low-Medium，推荐 Phase 2 Safe Probe） |

---

## 5. 接口契约验证

D-038 决议规格 vs 实际实现的逐项对照：

| # | 决议要求 | 实现位置 | 一致性 |
|---|----------|----------|--------|
| 1 | `search_fn` 签名：`(const int32_t*, size_t, int32_t, size_t*)` | [internal.h:L17](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L17) | ✅ 精确匹配 |
| 2 | 不传 `impl` 进入（保持分层隔离） | [api.c:L77](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L77) `impl->search_fn(impl->vals, impl->n, key, out_index)` | ✅ 仅传字段值 |
| 3 | `INT32_SEARCH_AVX2_MIN_N = 10,000,000` 编译期常量 | [internal.h:L11](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L11) `10000000ULL` | ✅ |
| 4 | `find()` 热路径单行间接调用 | [api.c:L77](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L77) | ✅ |
| 5 | Phase 2 B1 兼容（仅需替换 `search_fn` 赋值） | `create()` 中仅改 `search_fn` 赋值语句 | ✅ 架构满足 |
| 6 | `platform_cpu_has_avx2()` 语义不变 | 未修改 `platform_cpu.c` 核心逻辑，仅添加注释 | ✅ |
| 7 | `INT32SEARCH_FORCE_AVX2` 绕过阈值但仍检查 CPU | [api.c:L46-L51](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L46-L51) | ✅ 逻辑正确（但 CI 构建链路断裂） |

---

## 6. 文档交叉验证

| 文档 | 声称变更 | 实际确认 |
|------|----------|----------|
| `技术路线.md` §1 | D-008 修订 "Linux GCC 3.5x-5.1x … 已知退化" | ✅ 已确认 |
| `技术路线.md` §7 | 新增 AVX2 MinGW 退化风险行 | ✅ 已确认 |
| `FINAL_task_001_phase1_mvp.md` §8 | 新增"已知平台限制" | ✅ 已确认 |
| `INVESTIGATION_...` §1 | 新增 POC v1 对比基准 | ✅ 已确认（含 56.3% 错误率 + bug 假象说明） |
| `INVESTIGATION_...` §7.1 | 不确定度声明 → 升级确认结论 | ✅ 已确认 |
| `INVESTIGATION_...` §9 | 编译选项分析缺口说明 | ✅ 已确认 |
| `platform_cpu.c` | 头部注释 | ✅ [L4-L14](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/platform_cpu.c#L4-L14) 已确认 |
| `search_avx2.c` | 防御性注释 | ✅ [L30](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/search_avx2.c#L30) 已确认 |

---

## 7. 技术债务标注

| 编号 | 描述 | 优先级 | 关联发现 | 建议处理阶段 |
|------|------|--------|----------|-------------|
| [DEBT-AUDIT-01] | `test-force-avx2` Makefile 未重建携带 `-DINT32SEARCH_FORCE_AVX2` 的库 | HIGH | 发现 #1 | Phase 1 收尾（立即） |
| [DEBT-AUDIT-02] | `test_scalar_fallback` 缺少 sanitizer 编译标志 | LOW | 发现 #3 | Phase 1 收尾 |
| [DEBT-AUDIT-03] | `path` 字段 + `PATH_A`/`PATH_B1` 宏成为死代码 | LOW | 发现 #2 | Phase 2 B1 就绪后 |

---

## 8. 签收确认

- [ ] **人工确认签收** — 请审阅本报告及 [07_todo.md](07_todo.md) 后确认

> **本次审计结束，无更多自动处理。**
> 会议 meeting_005 的 12 项 TODO + 4 项 SEC 实质性工作均已完成。
> 发现 3 项偏差（1 HIGH / 1 MINOR / 1 LOW），详见 §3 偏差清单和 [07_todo.md](07_todo.md)。
> 后续行动：修复 FIX-01 后 meeting_005 可进入 Frozen。
