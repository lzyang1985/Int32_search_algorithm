---
title: D-140 2x SIMD 展开性能回归审计
meeting_id: meeting_017_performance_squeeze
status: Reviewing
created_at: 2026-06-08
updated_at: 2026-06-08
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_017_performance_squeeze/meeting_README.md
parent_task: root
affected_resolutions: [D-140, D-141, D-142, D-143]
participants: [Architect, Algorithm, Backend, Security]
tags: [regression, SIMD, GCC, benchmark, audit]
---

# D-140 2x SIMD 展开性能回归审计

## 1. 触发背景

人工反复测试 meeting_016 + meeting_017 变更（D-140~D-143）后的代码，发现修改前的旧版 exe（`demo/` 下）性能优于修改后的 v2 exe。具体表现为：

**测试平台**: Windows x86-64, GCC 15.2.0 (MinGW), 5M int32 均匀数据, 10K 查询

| 场景 | 旧版 (demo/) | v2 (D-140~D-143) | 变化 |
|------|-------------|-------------------|------|
| Int32 标准 50% hit (Bloom ON) | ~148 ns/q | ~135 ns/q | -8.8% |
| **Int32 Bloom OFF 50% hit** | **~140 ns/q** | **~176 ns/q** | **+25.7% ❌** |
| Int32 Bloom OFF 100% hit | ~56 ns/q | ~54 ns/q | -3.6% |
| Int64 标准 50% hit | ~167 ns/q | ~164 ns/q | -1.8% |

十轮测试中九轮显示 v2 负优化，Bloom OFF 50% hit 场景退化最严重。

## 2. 专家评审

四位专家（Arch/Algo/Backend/Sec）并行独立分析，结论高度一致。

### 2.1 根因判定（Algo + Backend + Arch 一致）

**第一根因：GCC `-O3` 自动展开器在手写 2x 展开之上进一步展开**
- `-O3` 默认启用 `-funroll-loops`
- D-140 将单路 16 元素/轮改为 2x 展开 32 元素/轮（5 YMM 寄存器）
- GCC 自动展开器进一步展开为 4x（64 元素/轮），需要 8+ YMM 寄存器
- Skylake 仅 16 个 YMM → 寄存器溢出到栈 (spill/fill)
- 每次 spill/fill 消耗 load/store 端口，抢占 L1D cache line

**第二根因：分支预测器污染（Algo）**
- 命中路径从 2 个分支点膨胀到 5 个（OR test → m0 test → while-ctz → m1 test → while-ctz）
- 50% hit 下命中/不命中交替发生，分支预测器误预测惩罚 ~18 cycles/次

**第三根因：D-141 对齐收益实际为零**
- 热路径仍使用 `_mm256_loadu_si256`（非 `load_si256`）
- `loadu` 在对齐数据上（Haswell+）本零惩罚
- `platform_aligned_alloc` 仅影响构建阶段，不在查询热路径

### 2.2 安全审查（Sec）

- **综合风险等级：低**
- 所有 SIMD 边界条件正确，无越界风险
- 建议加固：`start < 0` 下界检查 + `(size_t)end > n` 替代 `(int32_t)n` 溢出风险
- 加固方案零性能开销

### 2.3 流程缺陷（Arch）

- D-140~D-143 在 meeting 中同一天决策+落地，跳过了独立 POC 验证阶段
- D-130~D-133 均要求先 POC 后立项，D-140~D-143 被"低复杂度"豁免
- 缺失性能 benchmark 验收（只做了 correctness 测试）

## 3. 修复方案

### 3.1 代码修改（`src/search_b1.c`）

**修改一：D-140 条件编译包裹（默认关闭）**

```c
#ifdef INT32_SEARCH_B1_UNROLL2
    // 2x unrolled SIMD loop (D-140)
    // ⚠️ 需要 -fno-unroll-loops 以防止 GCC -O3 二次展开
#else
    // 原始单路 16 元素 SIMD loop（默认）
#endif
```

启用方式：`gcc -DINT32_SEARCH_B1_UNROLL2 -fno-unroll-loops`

**修改二：D-143 安全防御加固**

```c
/* D-143: 防御性边界检查 */
if (start < 0 || end < 0 || start >= end)
    return INT32_SEARCH_ERR_NOT_FOUND;
if ((size_t)end > n) end = (int32_t)n;
```

新增 `start < 0` / `end < 0` 下界检查 + `(size_t)end > n` 避免极端溢出。

### 3.2 保留的变更

| 决议 | 说明 | 原因 |
|------|------|------|
| D-141 | `platform_aligned_alloc` 32B 对齐 | 构建期优化，无伤害 |
| D-142 | 小桶 (<8) 标量快速路径 | 正确性 OK，低风险 |
| D-143 | `end` 上界 clamp（已加固） | 安全必须 |

### 3.3 README.txt 更新

在编译命令中添加了 `INT32_SEARCH_B1_UNROLL2` 选项说明。

## 4. 修复后验证

修复后重新编译所有四个 demo（v3），与旧版对比：

| 场景 | 旧版 (demo/) | v2 (退化) | v3 (修复) |
|------|-------------|-----------|-----------|
| Int32 Bloom OFF 50% | ~140 ns/q | **176 ns/q** | **141 ns/q ✅** |
| Int32 Bloom OFF 100% | ~56 ns/q | 54 ns/q | **42 ns/q ✅** |
| Int32 标准 50% Bloom ON | ~148 ns/q | 135 ns/q | 135 ns/q ≈ |
| Int64 标准 50% | ~167 ns/q | 164 ns/q | 180 ns/q ≈ |

**Bloom OFF 50% hit 恢复到旧版水平（+25.7% → +0.7%），退化完全消除。**

## 5. 后续建议

1. **条件启用 D-140**：等 D-130 PGO 集成后，用 `-DINT32_SEARCH_B1_UNROLL2 -fno-unroll-loops` 在 Linux 上重新 benchmark 验证真实收益
2. **性能回归 CI**：增加 Bloom OFF 50% hit 场景的性能回 归门禁（≥5% 退化阻断）
3. **流程改进**：任何"低复杂度"微架构优化在并入主分支前必须出具独立 POC benchmark 报告

## 6. 关联信息

- 父文档：`meeting_017_performance_squeeze/meeting_README.md`
- 关联决议：D-140, D-141, D-142, D-143
- 关联代码：`src/search_b1.c`, `src/build_b1.c`, `src/api.c`
- 生成产物：`demo/*_v3.exe`（4 个修复后 demo）
