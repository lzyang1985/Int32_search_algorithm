---
title: 讨论记录 — 第四波 Linux 验证结果评审
meeting_id: meeting_006_wave4_linux_verification_review
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_006_wave4_linux_verification_review/meeting_README.md
parent_task: task_001_phase1_mvp
---

# 讨论记录 — 第四波 Linux 验证结果评审

---

## 议题 1：VERIFY-01~04 结果逐项评审

### 架构项目经理发言

> VERIFY-01~03 联合满足 Phase 1 正确性和安全质量门控，但单独不足以宣告 Phase 1 整体质量达标。性能维度（VERIFY-04）揭示当前 Phase 1 交付代码在 Linux Xeon Gold 6152 上 AVX2 路径为 0.43x–0.48x vs 标量，与 FINAL 文档记载的 5.26x 存在巨大差异。FINAL §3 以「Phase 1 性能结果」之名呈现 POC v3 数据，未标注数据来源为非 Phase 1 代码路径，必须修正。

### 后端工程师发言

> 编译兼容性指标无可挑剔。GCC 9–12 全版本 `-Wall -Wextra` 零警告通过，ASan/UBSan/Valgrind 三工具零告警——很多 1.0 版本的开源库都达不到这个标准。但 benchmark 有一个 unused-variable warning 必须在收尾前修复。另外 CONFIRMED: FIX-02（溢出检查）和 FIX-03（NULL 守卫）在当前代码中已实装完毕。

### 算法工程师发言

> 撤回 meeting_005 中的「编译器缺陷」假说。D-037 实验 + VERIFY-04 Linux 数据构成完整证据链——根因是**算法结构性瓶颈**，非平台/编译器问题。引入「每周期信息增益比」框架：标量二分 0.286 bit/cy vs AVX2 SIMD 二分 0.250 bit/cy。三个瓶颈：`vmovmskps` 域跨越惩罚（~4 cy）、标量加载序列化依赖链（~17.25 cy/iter）、三路分支未减少不可预测分支数量。10M 阈值在所有层面均不合理。

### 代码安全专家发言

> ASan/UBSan + Valgrind 证明力为「强」，但存在 4 个工具链盲区：SIMD 向量内部访问不受 ASan 精确保护、语义正确的 SIMD 逻辑错误不会被捕获、`_mm_malloc` 的 Valgrind 兼容性非跨平台保证、并发场景完全未覆盖。FIX-02/03 **确认已实装**。double-destroy 测试应编写但期望应调整为「ASan 可检测违规」。强烈建议增加 fuzz 测试。

### 全栈开发发言

> API 简洁合格但存在 3 个 DX 缺陷：无 `strerror()` 函数、`typedef void*` 丢失类型安全、头文件零注释。README.txt 已包含 MinGW 命令，但引用了不存在的 Makefile。FIXPLAN 待办中 DOC-03 实际可能已完成。建议优先投入 FIX-01（统一错误码）和 DOC-03/README Makefile 修复。

---

## 议题 2：AVX2 性能瓶颈根因讨论

### 算法工程师深度分析

**信息效率框架**：

| 方案 | bit/iter | cy/iter | bit/cy |
|------|:---:|:---:|:---:|
| 标量二分 | 1 | 3.5 | **0.286** |
| AVX2 SIMD 二分 | 3 | 12+ | **0.250** |
| AVX-512 SIMD 二分（预测） | 4 | 20+ | **0.200** |

**三大瓶颈**：
1. **`vmovmskps` + 域跨越惩罚**：3 cy 延迟 + ~1 cy 旁路延迟
2. **`vals[last_le]` 标量加载序列化依赖链**：加权平均 ~17.25 cy/iter，OOO 引擎完全无法打破
3. **三路分支 vs 标量一路分支**：未减少不可预测分支，只换了位置

**结论**：SIMD 宽度越大，信息效率越低——这是反直觉但 benchmark 可证的。继续投入 AVX2 二分微优化是方向性错误。

### 全栈开发回应

> 同意算法工程师的分析。API 层 `INT32_SEARCH_AVX2_MIN_N` = 10M 阈值在当前交付物中无积极意义——既然 10M 时 AVX2 仍无加速，阈值应设为 `SIZE_MAX`（永久禁用当前 Path A AVX2）或直接移除。

### 架构项目经理汇总

> 算法工程师的「信息效率」模型解释了所有三个平台（Linux Xeon / Windows MinGW / WSL2）的一致表现。恢复 POC v3 的 ~170 cy/q 性能需要突破 SIMD 二分信息瓶颈，而非微调当前算法。这应在 Phase 2 DEEP-05 中量化分析。

---

## 议题 3：FIXPLAN 第五波（深度调查）是否仍需要

### 架构项目经理

> 同意取消第五波（DEEP-01~03）全部三项，将释放的 2 天工作量重定向至 DEEP-05「POC v3 vs Phase 1 算法差异性能影响分析」。VERIFY-04 已决定性地证明瓶颈跨平台一致。

### 后端工程师

> 从工程角度同意取消。4 个 GCC 版本 + Linux/Windows 跨平台一致表现已充分证明问题不在编译器/平台层。

### 代码安全专家

> 无安全层面异议。反汇编分析（DEEP-01）可降级为 Phase 2 内部技术备注。

**初步共识**：DEEP-01~03 取消，替换为 DEEP-05。

---

## 议题 4：Phase 1 收尾判定

### 架构项目经理

> Phase 1 收尾硬性条件清单（7 项 + 2 跟踪项）：
> 
> **硬条件**：
> - PC-01: ACCEPTANCE SR-05 状态更新为「✅ Linux 验证通过」
> - PC-02: FINAL §3 性能数据增加来源标注 + VERIFY-04 实际数据引用
> - PC-03: 第二波 DOC-01~04 全部完成
> - PC-04: FINAL §9 增加「性能数据来源差异」风险项
> - PC-05: 第一波执行状态修正为「已完成」
> - PC-06: 第五波 DEEP-01~03 正式取消，新增 DEEP-05
> - PC-07: TODO 文档闭合已完成项，新增 AVX2 性能回归跟踪
>
> **跟踪项**：
> - PT-01: AVX2 算法性能回归纳入 Phase 2 高优先级
> - PT-02: D-04 double-destroy 在 Phase 2 中期 API 层防御

### 代码安全专家补充

> Phase 2 启动时 10 项强制性安全措施（SEC-P2-01~10）必须纳入 DESIGN/TASK，不可降级。包括：TSan 覆盖、B1 COW 内存序验证、dir 边界验证、lo16 溢出检查、A vs B1 百万次交叉验证等。

### 全栈开发补充

> README 引用了不存在的 Makefile——建议要么补充 Makefile，要么从 README 删除 make 命令。另外头文件需追加 API 文档注释（至少 `@param`/`@return`/`@note` 级别）。

### 后端工程师补充

> benchmark unused-variable warning 需要修复。CMake 的 `include_directories` 应改为 target 级，Phase 2 执行即可。

---

## 讨论总结

| 主题 | 共识方向 | 异议 |
|------|----------|------|
| VERIFY-01~03 质量门控 | 安全/正确性维度通过，性能维度需重新表述 | 无 |
| FINAL 性能数据修正 | 必须增加 POC v3 vs Phase 1 来源标注 | 无 |
| 第二波强制完成 | 全部同意，DOC-03 可能已完成需确认 | 无 |
| 第五波取消 | DEEP-01~03 取消，新增 DEEP-05 | 无 |
| Phase 2 启动前提 | FINAL 修正 + 第一/二波完成 | 算法工程师建议增加 Path B1 微型 POC |
| AVX2 算法方向 | 不再投入微优化，转向 Eytzinger / S-tree / Path B1 | 无 |
| 安全强制项 | SEC-P2-01~10 纳入 Phase 2 硬性门控 | 无 |
| Fuzz 测试 | Phase 1 收尾前增加 P1 结构化 fuzz | 无 |
