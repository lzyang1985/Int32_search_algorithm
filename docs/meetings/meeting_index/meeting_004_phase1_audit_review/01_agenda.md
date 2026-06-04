---
title: 议程 — Phase 1 审计复核与 Windows 基准异常调查会
status: Frozen
created_at: 2026-05-29
meeting_id: meeting_004_phase1_audit_review
---

# 议程 — Phase 1 审计复核与 Windows 基准异常调查会

## 1. 上期回顾

### 1.1 Phase 1 MVP 交付总结

- 13/13 原子任务全部 SUCCESS
- Linux 性能：10M 数据 168.2 cy/q，5.26x 加速比 ✅
- 偏差清单：0 Critical / 1 Major（已修复）/ 1 Medium / 4 Minor
- 安全审计：无 Critical 级别问题
- TODO：9 项待办

### 1.2 关键发现：Windows 基准异常

| N | AVX2 (cy/q) | Scalar (cy/q) | 加速比 | Linux 加速比 |
|---|------------|--------------|--------|-------------|
| 1M | 840.8 | 410.0 | **0.49x** | 2.44x |
| 5M | 1336.8 | 692.3 | **0.52x** | 4.84x |
| 10M | 1534.5 | 847.7 | **0.55x** | 5.26x |

> ⚠️ Windows 上 AVX2 比标量慢约 2 倍，与 Linux 的 5.26x 加速比形成剧烈反差。

## 2. 议题讨论

### 议题 A：审计文档补充审查

**背景**：ACCEPTANCE 文档对 Windows 异常的诊断归因于 `__builtin_cpu_supports("avx2")` 可能返回 0，但此诊断缺乏充分证据。

**待讨论**：
1. 当前 ACCEPTANCE 文档第 107 行的诊断是否需要修正？
2. 是否需要新增独立的根因分析文档？
3. FINAL 报告中的 "Phase 2 准备度评估" 是否需要更新？

**相关文件**：
- [ACCEPTANCE_task_001_phase1_mvp.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/ACCEPTANCE_task_001_phase1_mvp.md#L101-L108)
- [FINAL_task_001_phase1_mvp.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/FINAL_task_001_phase1_mvp.md#L93-L97)

### 议题 B：Benchmark 方法论缺陷

**背景**：当前 benchmark 的 "AVX2" 路径通过 `int32_search_find`（完整 API），而 "Scalar 基线" 是内联裸循环，两者不是公平比较。

**待讨论**：
1. 应如何设计公平的对比方案？
2. 是否需要在 benchmark 中增加 `API+Scalar` 路径作为对照？
3. 是否需要直接调用 `search_avx2_find` 绕过 API 以排除 API 开销干扰？

**相关代码**：
- [bench_main.c:L18-L41](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/benchmark/bench_main.c#L18-L41) — `run_benchmark`（API 路径）
- [bench_main.c:L43-L76](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/benchmark/bench_main.c#L43-L76) — `run_scalar_bench`（内联裸循环）
- [api.c:L52-L71](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L52-L71) — `int32_search_find` 路径

### 议题 C：AVX2 慢于标量的根因假说

**假说 1**：`__builtin_cpu_supports("avx2")` 返回 1 (AVX2 确实在跑)，但代码路径存在性能陷阱：
- `_mm256_movemask_ps` 域转换惩罚
- GCC 15.2.0 Windows 移植的 AVX2 代码生成质量
- `popcount` 实现路径（硬件 popcnt vs 软件模拟）

**假说 2**：`__builtin_cpu_supports("avx2")` 返回 0（标量回退），额外开销来自：
- API 调用链开销（多次函数调用 + 多次分支检查）
- CPU 频率缩放导致的计时偏差
- 分支预测器污染

**假说 3**：其他因素：
- 数据对齐问题（benchmark 数据用 `malloc` 而非 `_mm_malloc`）
- `_mm256_loadu_si256` 非对齐加载的跨 cache line 惩罚

**待讨论**：
1. 哪个假说最可能？如何设计最小实验验证？
2. 是否需要生成反汇编检查 GCC 15.2.0 的实际代码生成？
3. 是否需要对比不同编译器（clang/msvc）的结果？

### 议题 D：调查方案和实施计划

**待讨论**：
1. 验证实验的优先级排序
2. 谁执行哪些步骤（Agent vs 人工）
3. 调查的时间窗口
4. 调查结果如何回流到文档体系

## 3. 决议与待办

（待各专家讨论后汇总）
