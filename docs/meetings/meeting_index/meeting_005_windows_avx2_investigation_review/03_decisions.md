---
title: 决议记录 — Windows AVX2 异常调查结果审查会
status: Frozen
created_at: 2026-05-29
updated_at: 2026-05-30
author: Agent_Executor
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/meeting_README.md"
---

# 决议记录 — Windows AVX2 异常调查结果审查会

## 决议总览

| 编号 | 标题 | 投票结果 | 严重程度 |
|------|------|----------|----------|
| D-034 | 接受调查方法论，标注根因分析不确定度 | 4/4 通过 | Major |
| D-035 | AVX2 路径保留 + 构建时决策方案 + 标记已知限制 | 4/4 通过 | Major |
| D-036 | 修订技术路线文档 D-008 表述 | 4/4 通过 | Major |
| D-037 | 新增 P0：本平台编译选项对比实验；原 P2 Linux 验证降级 P3 | 4/4 通过 | Critical |
| D-038 | 采纳后端工程师"构建时函数指针"方案，消除查询热路径 CPU 检测冗余 | 4/4 通过 | Minor |
| D-039 | 安全增强：防御性断言 + CI 保留 AVX2 编译配置 | 4/4 通过 | Minor |
| D-040 | 调查文档修正：补充编译选项分析、标注根因不确定度 | 4/4 通过 | Minor |

---

## D-034：接受调查方法论，标注根因分析不确定度

**决议内容：**

1. INVEST-01~08 的调查方法论**充分**，公平 benchmark 设计**可信**。
2. 但 §7 根因分析（"AVX2 每次迭代 ~12 cy 是减速根本原因，只在 cache miss 主导时才有优势"）**未解释 POC v1 在同平台 Windows 上 120 cy/q (3.53x) vs 当前代码 951 cy/q 的 8 倍差异**，指令延迟分析只能解释约 13% 的差距。
3. INVESTIGATION 报告的 §7.1 中增加不确定度声明：当前结论为"暂定假说"，待 D-037 编译选项对比实验后最终确认。
4. 在 §9 调查结论中标注"根因待编译选项对比实验（D-037）最终确认"。

**投票**：Architect ✅ / Algorithm ✅ / Backend ✅ / Security ✅ → **4/4 通过**

---

## D-035：AVX2 路径保留 + 构建时决策方案 + 标记已知限制

**决议内容：**

1. **AVX2 代码路径保留**，不降级、不删除。Linux 上 5.26x 加速比（ACCEPTANCE 记录）已验证且达标。
2. **不采用"默认走标量"策略**。改为**"构建时决策"方案**（见 D-038）：
   - `int32_search_create()` 中根据 CPU 能力 + 数据规模 N 一次性选定搜索函数指针
   - 阈值 `INT32_SEARCH_AVX2_MIN_N = 10,000,000`（编译期常量）
   - 热路径 `int32_search_find()` 仅做 `return impl->search_fn(...)` 调用
3. 在 FINAL_task_001_phase1_mvp.md 中新增"已知平台限制"条目：
   - Windows MinGW GCC 15.2.0 AVX2 路径性能异常（0.45x-0.55x vs 标量），根因待 D-037 实验确认
   - Linux 生产环境不受影响（5.26x 加速比已验证）
4. 在 `src/platform_cpu.c` 头部增加注释：`platform_cpu_has_avx2()` 仅检测 CPU 硬件能力，不反映编译器代码生成质量。

**投票**：Architect ✅ / Algorithm ✅ / Backend ✅ / Security ✅（附条件：保留 AVX2 CI）→ **4/4 通过**

---

## D-036：修订技术路线文档 D-008 表述

**决议内容：**

1. [技术路线.md §1 表格](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/architecture/技术路线.md#L21) "查询算法"行修订：

   **修订前**：`AVX2 8 路块状 SIMD 二分（Path A）| D-008：3.5x-5.1x vs 标量`

   **修订后**：`AVX2 8 路块状 SIMD 二分（Path A）| D-008：Linux GCC 3.5x-5.1x vs 标量；Windows MinGW 已知退化（0.45x-0.55x，见 §7 风险）`

2. 技术路线.md §7 已知风险表新增条目：

   | 风险 | 等级 | 缓解措施 |
   |------|------|----------|
   | AVX2 MinGW 代码生成退化 (0.45x vs 标量) | 高 | Phase 2 构建时 microbenchmark 自检；Phase 3 多编译器评估；短期不阻塞 Linux 交付 |

3. D-008 原始决议（"AVX2 SIMD 向量化二分查找为唯一查询路径"）在 Linux 目标平台语境下**维持有效**，不被推翻。
4. D-011（"方案 A 为最终且唯一的查询算法路径"）在 Linux 语境下仍成立。

**投票**：Architect ✅ / Algorithm ✅ / Backend ✅ / Security ✅ → **4/4 通过**

---

## D-037：新增 P0 编译选项对比实验；原 P2 Linux 验证降级 P3

**决议内容：**

1. **新增 P0 任务**：在 Windows 本机执行编译选项对比实验（预计 30 分钟）：

   | 实验 | 编译选项 | 目的 |
   |------|---------|------|
   | E1 | `-O3 -mavx2`（当前基线） | 确认 951 cy/q 可复现 |
   | E2 | `-O3 -mavx2 -march=native` | 验证是否能恢复到 ~150 cy/q |
   | E3 | E2 的汇编输出 vs E1 | 定位编译器生成差异的指令级位置 |
   | E4 | POC 的 `search_avx2_binary` 拷入当前项目编译 | 排除代码逻辑差异 |

2. **如果 E2 确认 `-march=native` 恢复性能**：
   - 根因重新定性为"编译器优化选项不足"
   - 技术路线 D-008 的"3.5x-5.1x"全平台有效
   - Makefile 默认使用 `-march=native`（或检测到 Skylake+ 自动启用）

3. **如果 E2 仍为 0.45x-0.55x**：
   - 根因在于 POC 和当前代码的算法逻辑差异（早期退出、分支结构等）
   - 对照 POC 修正 `search_avx2_find` 的搜索逻辑

4. **原 FIXPLAN 第四波 Linux 交叉验证（VERIFY-04）优先级从 P2 降级为 P3**。理由：POC 已证明同一硬件 + Windows 平台可达 3.53x，跨平台对比无法定位根因。

**投票**：Architect ✅ / Algorithm ✅ / Backend ✅ / Security ✅ → **4/4 通过**（关键路径 P0）

---

## D-038：采纳"构建时函数指针"方案，消除查询热路径 CPU 检测冗余

**决议内容：**

1. 在 `int32_search_impl_t`（internal.h）中增加 `search_fn` 函数指针字段：
   ```c
   int32_t (*search_fn)(const int32_t *vals, size_t n, int32_t key, size_t *out_index);
   ```

2. `int32_search_create()` 中一次性完成 CPU 检测 + 路径决策 + 函数指派：
   ```c
   impl->avx2_capable = (uint8_t)platform_cpu_has_avx2();
   if (impl->avx2_capable && impl->n > INT32_SEARCH_AVX2_MIN_N)
       impl->search_fn = search_avx2_find;
   else
       impl->search_fn = search_scalar_find;
   ```

3. `int32_search_find()` 热路径简化为：
   ```c
   return impl->search_fn(impl->vals, impl->n, key, out_index);
   ```
   保留 `handle==NULL` / `out_index==NULL` 防御性检查。

4. 定义编译期常量 `#define INT32_SEARCH_AVX2_MIN_N 10000000ULL`

**投票**：Architect ✅ / Algorithm ✅ / Backend ✅（提案人）/ Security ✅ → **4/4 通过**

---

## D-039：安全增强措施

**决议内容：**

1. 在 `search_avx2.c` 循环内添加防御性注释/断言，标注 `block = hi - 8` 依赖于 `while (hi - lo >= 8)` 循环条件（防御深度不足，Medium）。
2. CI 中保留 `INT32SEARCH_FORCE_AVX2` 编译宏配置，确保 AVX2 代码在"默认标量"策略下不会腐烂。
3. 安全跟踪表新增 4 条记录（SEC-01 ~ SEC-04，详见 action_items 文档）。

**投票**：Architect ✅ / Algorithm ✅ / Backend ✅ / Security ✅（提案人）→ **4/4 通过**

---

## D-040：调查文档修正

**决议内容：**

1. INVESTIGATION 报告 §7.1 增加不确定度声明，标注"暂定假说，待 D-037 确认"。
2. §9 调查结论中增加编译选项分析缺口说明。
3. §1 测试环境表格增加 POC v1 对比行（`-march=native`，120 cy/q）。
4. 补充重复测量标准差数据（N=5）。

**投票**：Architect ✅ / Algorithm ✅ / Backend ✅ / Security ✅ → **4/4 通过**

---

## 决议汇总

本次会议共形成 **7 项决议**，全部 **4/4 全票通过**，无意见冲突。

| 编号 | 标题 | 优先级 | 执行阶段 |
|------|------|--------|----------|
| D-034 | 接受方法论 + 标注根因不确定度 | -- | 文档修正 |
| D-035 | AVX2 保留 + 构建时决策 + 已知限制 | P0 | Phase 1 收尾 |
| D-036 | 修订技术路线文档 D-008 | P0 | Phase 1 收尾 |
| D-037 | P0 编译选项对比实验；P2→P3 Linux 验证 | P0 | 立即执行 |
| D-038 | 构建时函数指针方案 | P0 | Phase 1 收尾 |
| D-039 | 安全增强 | P2 | Phase 1 收尾 |
| D-040 | 调查文档修正 | P1 | Phase 1 收尾 |

> **会议状态：决议完成，转入待办事项跟踪。**
