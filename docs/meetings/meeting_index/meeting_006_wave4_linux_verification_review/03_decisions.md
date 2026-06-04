---
title: 决议 — 第四波 Linux 验证结果评审
meeting_id: meeting_006_wave4_linux_verification_review
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_006_wave4_linux_verification_review/meeting_README.md
parent_task: task_001_phase1_mvp
---

# 决议 — 第四波 Linux 验证结果评审

---

## D-041：VERIFY-01~03 通过 Phase 1 安全/正确性质量门控

**决议**：VERIFY-01（ASan/UBSan 零告警）、VERIFY-02（Valgrind 零泄漏）、VERIFY-03（GCC 9-12 全兼容 `-Wall -Wextra` 零警告）三项验证共同满足 Phase 1 安全与正确性质量门控。

- SR-05（ASan/UBSan 零告警）从「⚠️ 待验证」→「✅ Linux 验证通过」
- 内存泄漏实证闭合
- GCC 向上兼容性扩展验证通过

**投票**：5/5 通过。

---

## D-042：FINAL 文档性能数据必须修正

**决议**：FINAL_task_001_phase1_mvp.md §3 当前以「Phase 1 性能结果」之名呈现 POC v3 数据（5.26x @ 10M），未标注数据来源为非 Phase 1 交付代码路径。必须增加明确声明：

> "以上数据来自 POC v3 原型验证（使用 `_mm256_cmpgt_epi32(key, vec)` 路径，存在约 50% 命中查询返回 NOT_FOUND 的正确性缺陷）。Phase 1 交付代码经 D-01 正确性修复后，AVX2 路径当前延迟为 Raw AVX2 ~2852 cy/q @ 10M (Xeon Gold 6152)。AVX2 算法性能恢复已纳入 Phase 2/3 优化计划。"

**投票**：5/5 通过。

---

## D-043：第一波（FIX-01~05）状态正式修正为「已完成」

**决议**：经代码安全专家和后端工程师双重确认，FIX-02（溢出检查）和 FIX-03（NULL 守卫）在当前代码中已实装。结合 [ACCEPTANCE_FIXPLAN_wave1](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/ACCEPTANCE_FIXPLAN_wave1_task_001_phase1_mvp.md) 记录，第一波 FIX-01~05 全部已实装并编译验证通过。FIXPLAN 和 TODO 文档应标记第一波为「已完成」。

- FIX-01 (统一错误码): ✅ 已在 internal.h / search_scalar.c / search_avx2.c 实装
- FIX-02 (溢出检查): ✅ 已在 build_sorted.c L30 实装
- FIX-03 (NULL 守卫): ✅ 已在 platform_memory.c L11 实装
- FIX-04 (double-destroy 测试): ✅ 已添加
- FIX-05 (benchmark 种子): ✅ 已实装

**投票**：5/5 通过。

---

## D-044：第二波（DOC-01~04）强制纳入 Phase 1 收尾前置条件

**决议**：DOC-01~04（0.2 天工作量，消除 4 个 Minor 偏差）必须在 Phase 1 正式收尾前完成。不阻塞 Phase 2 启动，但与 Phase 2 并行执行期间 Phase 1 不能宣告 Frozen。

- DOC-01: DESIGN §2.3.2 伪代码修正
- DOC-02: DESIGN §2.4.3 CPU 回退标注
- DOC-03: README.txt 确认/补充 MinGW 命令（全栈开发指出可能已存在）
- DOC-04: search_avx2.c `_mm256_movemask_ps` 行增加注释

**投票**：5/5 通过。

---

## D-045：第五波（DEEP-01~03）取消，新增 DEEP-05

**决议**：FIXPLAN 第五波 DEEP-01（反汇编跨平台对比）、DEEP-02（跨编译器对比）、DEEP-03（WSL2 vs 裸机）全部取消。VERIFY-04 已决定性证明 AVX2 性能瓶颈为算法级结构性瓶颈，与平台/编译器无关。释放的 2 天工作量重定向至：

**DEEP-05: POC v3 vs Phase 1 算法差异性能影响分析** — 定量解释 D-01 正确性修复（`_mm256_cmpgt_epi32(key,vec)` → `_mm256_cmpgt_epi32(vec,key)`）为何引入 ~17 倍 AVX2 延迟退化（168.2 → 2851.7 cy/q @ 10M）。

DEEP-04（`__builtin_cpu_supports` 假阳性评估）维持 Frozen 状态。

**投票**：5/5 通过。

---

## D-046：Phase 2 AVX2 算法方向调整

**决议**：基于算法工程师的「信息效率」分析框架（SIMD 0.250 bit/cy < 标量 0.286 bit/cy），**不再继续投入当前 Path A AVX2 SIMD 二分的微优化**。Phase 2 算法优先级调整为：

| 优先级 | 方案 | 说明 |
|:---:|------|------|
| P0 | Path B1（lo16 跳过 + 构建时分桶） | 已规划，正交于 Path A 瓶颈 |
| P0 | Eytzinger layout + branchless binary search | 零误预测，理论 ~0.18 bit/cy |
| P1 | S-tree（16-key B-tree + SIMD 节点查找） | 理论 0.333 bit/cy，需更多实现工作 |
| — | Path A AVX2 微优化 | **方向性错误，停止投入** |

**投票**：5/5 通过。

---

## D-047：10M AVX2 阈值处置

**决议**：鉴于 VERIFY-04 证明 10M 规模 AVX2 路径仍无加速（Raw 0.47x），`INT32_SEARCH_AVX2_MIN_N = 10,000,000` 阈值在所有层面均无积极意义。处置方案：

- 当前 Phase 1：将阈值调整为 `SIZE_MAX`（实质禁用当前 Path A AVX2，标量路径全面接管）
- Phase 2：B1 路径的 crossover 决策由 `max_bucket <= 150` 逻辑接管，不依赖此宏

**投票**：5/5 通过。

---

## D-048：Phase 2 启动前提条件

**决议**：Phase 2 可在满足以下前提后启动，不与 Phase 1 剩余收尾阻塞：

1. **PC-02**: FINAL 性能数据修正完成（D-042）
2. **PC-04**: FINAL §9 增加「性能数据来源差异」风险项
3. **SEC-P2-01~10**: Phase 2 DESIGN/TASK 中纳入 10 项安全强制门控

Phase 2 可与第二波（DOC-01~04）并行推进，但 Phase 1 Frozen 需等待两者均完成。

**投票**：5/5 通过。

---

## D-049：Fuzz 测试纳入 Phase 1 收尾

**决议**：Phase 1 收尾前增加 P1 结构化 fuzz 测试。采用代码安全专家推荐的 libFuzzer + ASan 方案（~30 分钟实现，60 秒运行），覆盖：随机 `n` （含非对齐边界）、随机 key（含 INT32_MIN/MAX）、与 `bsearch()` 交叉验证。结果写入 ACCEPTANCE 文档。

**投票**：5/5 通过。

---

## D-050：README 与 头文件 DX 改进

**决议**：
1. README.txt 中检查 Makefile 引用：若 Makefile 不存在则删除 `make` 相关命令段落，替换为直接编译命令；若 Makefile 存在则确认可用
2. `include/int32_search.h` 增加 API 文档注释：每个函数至少包含 `@param`、`@return`、`@note`（destroy 幂等性、find 的 out_idx 在 miss 时行为）
3. benchmark 种子环境变量 `INT32SEARCH_BENCH_SEED` 在 README 中记录用法

**投票**：5/5 通过。

---

## D-051：Benchmark 编译警告修复

**决议**：benchmark 编译产生的 1 个 minor unused-variable warning 必须在收尾前修复，保持全项目 `-Wall -Wextra` 零警告一致性。

**投票**：5/5 通过。

---

## D-052：本次会议 Frozen 后自动更新 FINAL/TODO/FIXPLAN

**决议**：Agent_Executor 在 meeting_006 Frozen 后执行以下文档同步：
1. ACCEPTANCE SR-05 状态更新（→ ✅ Linux 验证通过）
2. FINAL §3 增加性能数据来源标注（D-042）
3. FINAL §9 增加「性能数据来源差异」风险项
4. TODO 闭合已完成项（TODO-01/02/03/05/07/S-TODO-01/02/04），新增 AVX2 性能回归跟踪 + DEEP-05 + fuzz 测试待办
5. FIXPLAN 更新：第一波→已完成、第五波→已取消+DEEP-05 新增、第四波→已完成
6. meeting_index.md 注册 meeting_006

**投票**：5/5 通过。

---

> **本次会议 9 项决议全票通过（5/5）。**
> 会议状态：Draft → Reviewing，待人工确认后 Frozen。
