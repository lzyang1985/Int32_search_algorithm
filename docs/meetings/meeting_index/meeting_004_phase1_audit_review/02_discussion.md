---
title: 讨论记录 — Phase 1 审计复核与 Windows 基准异常调查会
status: Frozen
created_at: 2026-05-29
meeting_id: meeting_004_phase1_audit_review
participants: [Architect, Algorithm, Backend, Security]
---

# 讨论记录 — Phase 1 审计复核与 Windows 基准异常调查会

## 1. 专家意见汇总

### 1.1 架构项目经理评估

**核心判断**：Windows 异常反映的不是 AVX2 算法的实质性问题，而是 MinGW GCC 15.2.0 编译器特定版本的代码生成质量问题。

**关键建议**：
- **Phase 2 不延期**：Path B1 内核使用 `_mm256_movemask_epi8` 而非 `movemask_ps`，不依赖争议路径
- **P0 立即修正 ACCEPTANCE 第 107 行**：当前诊断 "可能返回 0 导致回退到标量" 证据不足且逻辑不完整
- 新增独立子任务 `task_002_windows_avx2_investigation`，与 Phase 2 并行推进
- 风险缓解：Linux 全部测试通过（正确性+边界+性能）证明算法本身正确

### 1.2 算法工程师评估

**根因判断**：`__builtin_popcount` 在 Windows GCC 15.2.0 `-mavx2` 下未正确启用 `TARGET_POPCNT`，走软件库调用路径，每次迭代额外 ~30-50 cycles。

**定量验证**：N=1M 约 20 次 AVX2 迭代，20 × 47 cycles（软件 popcount）= 940 cycles，完全解释 840.8 - 410.0 = 430.8 cy 的额外开销（考虑 CPU/OS 基准差异后吻合）。

**P0 实验**：
1. 对比 Windows/Linux 反汇编 → 确认 `call __popcountsi2` vs `popcntl`
2. 替换 `__builtin_popcount` 为 `_mm_popcnt_u32()`（来自 `<nmmintrin.h>`，SSE4.2 intrinsic）

**次要发现**：
- `_mm256_movemask_ps` 域转换在 Haswell+ 上无跨域惩罚（执行单元已统一）
- `_mm256_set1_epi32(target)` 可提至循环外（微优化，~60 cy/q）
- 当前算法缺少 POC v3 的提前退出优化（影响 50% 命中率场景）

### 1.3 后端/系统工程师评估

**Benchmark 方法论核心缺陷**："AVX2" 走完整 API 路径（6 次分支检查 + 2 次跨 TU 调用），"Scalar" 是内联裸循环（零开销）。

**API 层开销估算**：~24-38 cycles/query（函数调用 + 冗余 NULL 检查 + 分支），仅解释一小部分差距。

**公平对比方案**：需 5 组对照（API+AVX2 / API+Scalar / Raw AVX2 / Raw Scalar / Inline Scalar）。

**系统层面因素**（三者叠加可解释 2x 差距）：
- 频率缩放：AVX2 先跑时 CPU 仍在 Turbo ramp-up 阶段，Scalar 后跑时 Turbo 已激活
- RDTSC 无序列化：`__rdtsc()` 无 `lfence`，乱序执行污染测量
- 跨核迁移：Windows 调度器可能将 benchmark 迁移到不同核心

**7 步调查实验排序**（P0→P3），最经济方案：Agent 执行 Step 1（公平对照组）+ Step 4（数据对齐），人工在目标机跑一次。

### 1.4 代码安全专家评估

**安全审计结论**：Phase 1 代码无 Critical 安全漏洞。`_mm256_movemask_ps(_mm256_castsi256_ps(cmp))` 是 Intel 标准惯用法，无 strict aliasing 问题。

**新发现的安全遗漏项**（4 项，建议补充至 TODO）：

| 编号 | 严重程度 | 描述 |
|------|----------|------|
| S-TODO-01 | Medium | `n * sizeof(int32_t)` 无溢出检查（32 位平台风险） |
| S-TODO-02 | Low | `platform_aligned_free` 缺少显式 NULL 守卫（`_mm_free(NULL)` 行为实现定义） |
| S-TODO-03 | Medium | `__builtin_cpu_supports` 假阳性可致 AVX2 指令 #UD 崩溃（虚拟机/模拟器场景） |
| S-TODO-04 | Low | Benchmark `srand(time(NULL))` 不可重现，建议支持环境变量种子 |

**补充假说**：CPU 检测假阴性 → 标量回退。验证方法：在 benchmark 中打印 `platform_cpu_has_avx2()` 返回值。

---

## 2. 交叉讨论与共识

### 2.1 根因收敛分析

四位专家在以下判定上达成一致：

```
根因金字塔（可信度从高到低）
        ┌─────────────────────┐
        │ __builtin_popcount  │  ← Algorithm (P0), 最可能单一原因
        │ 走软件模拟路径      │     Backend 也怀疑此路径
        ├─────────────────────┤
        │ GCC/MinGW 代码生成  │  ← Backend (YMM寄存器溢出)
        │ 质量差              │     Architect (编译器问题)
        ├─────────────────────┤
        │ Benchmark 方法论    │  ← 全员确认：不公平对比
        │ 系统性偏差           │
        ├─────────────────────┤
        │ 频率缩放 + RDTSC    │  ← Backend (系统性偏向Scalar)
        │ 计时不准确           │
        └─────────────────────┘
```

**单一最可疑假说**（Algorithm 提出）：`__builtin_popcount` 在 Windows GCC 15.2.0 `-mavx2` 下未发射 `popcnt` 硬件指令，走 `__popcountsi2` 库调用。

**验证成本**：零代码改动。仅需 `gcc -S` 生成汇编，grep 查找 `call` vs `popcnt`。

### 2.2 Benchmark 方法论修正必要性

**全员一致同意**：当前 benchmark 的 "AVX2 vs Scalar" 对比方法需要修正。

Architect 建议：新增 D-07 偏差记录。
Backend 建议：5 组对照 + 交错测量 + RDTSCP + 绑核。
Algorithm 建议：修正后重新采集 Windows 基准数据。

### 2.3 Phase 2 启动判定

**Architect（最终决策权）**：Phase 2 不延期。前提：
1. ACCEPTANCE 第 107 行修正（P0，Phase 2 硬前置）
2. TODO/FINAL 同步（P0，Phase 2 硬前置）
3. 人工确认 Phase 2 启动

Security 补充：S-TODO-01~03 不阻塞 Phase 2，但应在 Phase 2 开发中同步处理。

### 2.4 平台约定重新确认

当前 ALIGNMENT 约定："Windows 验证非强制"。
新共识："Windows 验证非强制，但 Windows 异常需独立跟踪。不阻塞 Linux 交付。"
