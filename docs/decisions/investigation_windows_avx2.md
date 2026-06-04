---
title: Windows AVX2 异常调查报告 — Phase 1
status: Frozen
created_at: 2026-05-29
updated_at: 2026-05-30
author: Agent_Executor
task_id: task_001_phase1_mvp
parent_doc: "docs/tasks/task_001_phase1_mvp/FIXPLAN_task_001_phase1_mvp.md"
parent_task: task_001_phase1_mvp
source_meeting: meeting_004_phase1_audit_review, meeting_005_windows_avx2_investigation_review
tags: [investigation, avx2, windows, benchmark, performance]
---

# Windows AVX2 异常调查报告 — Phase 1

> 本报告执行 FIXPLAN 第三波（Windows AVX2 异常调查）全部任务，
> 涵盖 INVEST-01 ~ INVEST-08。

---

## 1. 测试环境

| 项目 | 值 |
|------|-----|
| CPU | Intel Xeon Processor (Skylake, IBRS) |
| OS | Windows (MinGW-w64) |
| 编译器 | GCC 15.2.0 |
| 编译选项 | `-O3 -std=c11 -mavx2` |
| `platform_cpu_has_avx2()` | **yes** ✅ |
| `__builtin_cpu_supports("avx2")` | **yes** ✅ |

### POC v1 对比基准

| 项目 | 值 |
|------|-----|
| POC v1 编译器 | GCC 15.2.0（同版本） |
| POC v1 编译选项 | `-O3 -std=c11 -mavx2 -march=native` |
| POC v1 性能 (N=1M) | ~120 cy/q (3.53x vs 标量) |
| POC v1 正确性 | ❌ **56.3% 错误率**（`mask ^ 0xFF` 逻辑错误） |
| 说明 | POC v1 的 3.53x 加速比是 **bug 假象**，修正后 AVX2 无法超越标量（见 TODO-01 E4 实验确认） |

---

## 2. INVEST-01: popcount 指令发射验证

### 方法

```bash
gcc -O3 -mavx2 -S src/search_avx2.c -o src/search_avx2_win.s
grep -i "popcnt\|popcountsi2" src/search_avx2_win.s
```

### 结果

```asm
popcntl	%r10d, %r10d
```

| 检查项 | 状态 |
|--------|------|
| 硬件 `popcntl` 指令 | ✅ 已发射 |
| 软件模拟 `__popcountsi2` | ❌ 未出现 |

**结论**: `__builtin_popcount` 在 Windows GCC 15.2.0 上正确生成了硬件 POPCNT 指令，不存在软件模拟回退。

---

## 3. INVEST-02: CPU 检测状态确认

### 方法

在 benchmark 中添加两种检测方式并打印：

```c
printf("AVX2 detected (platform_cpu_has_avx2): %s\n", ...);
printf("AVX2 detected (__builtin_cpu_supports): %s\n", ...);
```

### 结果

```
AVX2 detected (platform_cpu_has_avx2): yes
AVX2 detected (__builtin_cpu_supports): yes
```

**结论**: CPU 检测**正确**，AVX2 路径确实被执行。排除了"假阴性回退到标量"的假说。

---

## 4. INVEST-03: popcount 修复评估

### 判定

| 条件 | 结果 |
|------|------|
| INVEST-01 是否发现软件模拟 | 否（硬件 `popcntl` 已发射） |

**结论**: **不需要执行** INVEST-03。`__builtin_popcount` 在 Windows 上工作正常。

---

## 5. INVEST-04/05/06/07/08: 公平 Benchmark（改造 + 运行）

### 改造内容

| 编号 | 改造项 | 改动 |
|------|--------|------|
| INVEST-05 | 4 组公平对照 | API(AVX2) / Raw AVX2 / Raw Scalar / Inline Scalar |
| INVEST-06 | 计时改进 | `__rdtsc` → `__rdtscp()` + `_mm_lfence()` |
| INVEST-07 | 长预热 | 100 queries → ~500ms 循环预热（消除 Turbo ramp-up） |
| INVEST-08 | CPU 信息打印 | CPUID 型号名 + AVX2 检测状态 + 编译器版本 |

### Benchmark 结果

```
=== Fair Benchmark (4-group) ===
Group                                    cy/query
----------------------------------------------------------------------
                    N=1M    N=5M    N=10M
----------------------------------------------------------------------
01 API (AVX2)       1080.7  1475.4  1843.5
02 Raw AVX2          951.1  1279.1  1762.3
03 Raw Scalar        431.0   699.7   837.6
04 Inline Scalar     409.9   684.3   800.4
----------------------------------------------------------------------
AVX2 vs Raw Scalar    0.45x   0.55x   0.48x   ← AVX2 更慢!
API AVX2 vs Inline    0.38x   0.46x   0.43x   ← 含 API 开销更慢
```

### 关键发现

1. **AVX2 在所有数据规模上均慢于标量实现**：加速比 0.45x–0.55x（即 AVX2 约为标量速度的一半）
2. **API 封装开销约 80–180 cy/q**（API AVX2 vs Raw AVX2 差值）
3. **函数调用开销约 20–40 cy/q**（Raw Scalar vs Inline Scalar 差值）
4. **CPU 检测正确（AVX2=yes），AVX2 路径确实在执行**

---

## 6. 汇编级深度分析

### search_avx2_find 核心循环（GCC -O3 -mavx2, Windows）

```asm
.L8:
    ; ... 地址计算 (andq $-8, 对齐到 8 边界) ...
    vmovdqu   (%r11,%rax,4), %ymm0    ; [1] 加载 8 个 int32   (port 2/3)
    vpcmpgtd  %ymm1, %ymm0, %ymm0     ; [2] vec > key 比较    (port 0/1/5)
    vmovmskps %ymm0, %r10d            ; [3] 提取符号位为掩码   (port 0)
    popcntl   %r10d, %r10d            ; [4] 硬件 popcount     (port 1)
    subl      %r10d, %edi             ; [5] le_count = 8 - pc
    jne       .L25                     ; [6] 条件分支 → 二分逻辑
```

### 关键路径延迟分析 (Skylake)

| 指令 | 延迟 | 端口 | 备注 |
|------|------|------|------|
| `vmovdqu` | ~5 cy | p2/p3 | L1 命中 |
| `vpcmpgtd` | 1 cy | p0/p1/p5 | |
| `vmovmskps` | 3 cy | p0 | **整数→浮点域跨越，~1 cy 旁路延迟** |
| `popcntl` | 3 cy | p1 | |
| **关键路径总延迟** | **~12 cy/iter** | | |

### 与标量对比

| 指标 | AVX2 | Scalar |
|------|------|--------|
| 每迭代延迟 | ~12 cy | ~3-4 cy |
| log2 迭代数 (1M) | ~17 | ~20 |
| log2 迭代数 (10M) | ~20 | ~24 |
| 理论最小延迟 (1M) | ~204 cy | ~60-80 cy |
| 分支预测失败 (~50%) | +~75 cy | +~150 cy |
| **理论净延迟 (1M)** | **~280 cy** | **~210 cy** |

---

## 7. 根因分析

### 7.1 主要根因：算法效率

> **不确定度声明**（meeting_005 D-034/D-040）：本文 §7 根因分析初稿标注为"暂定假说，待 D-037 编译选项对比实验最终确认"。
> 2026-05-30 更新：TODO-01 编译选项对比实验（E1-E4）已完成，双平台（Windows + Linux）验证如下：
> - E2 `-march=native` 实验未恢复性能（AVX2 0.45-0.48x vs 标量），排除编译器代码生成质量假说
> - E4 POC 修复版对比确认原 POC "3.53x" 为 bug 假象，正确实现下 AVX2 无法超越标量
> - **结论升级为确认结论**：根因为算法层面效率瓶颈，非编译器、指令选择或代码 bug。

AVX2 SIMD 二分搜索的**SIMD 宽度不足以抵消每次迭代的额外开销**：

- AVX2 每次迭代处理 8 个元素，log2 减少约 log2(8)=3 次迭代
- 但每次 AVX2 迭代的延迟是标量的 **~3×**（12 cy vs 3-4 cy）
- 对于 N=1M–10M 规模，迭代减少 3 次无法抵消每次迭代多出的 ~8 cy 开销

**结论**：这是一种**算法层面**的问题，而非编译器或指令选择问题。AVX2 SIMD 辅助二分搜索只在大规模数据（N >> 10M，cache miss 主导时）才可能优于纯标量二分搜索。

### 7.2 次要因素

| 因素 | 影响 | 说明 |
|------|------|------|
| `vmovmskps` 跨域 | ~1 cy/iter | 整数→浮点域旁路延迟，轻微 |
| API 封装开销 | ~80-180 cy/q | `int32_search_find` → `platform_cpu_has_avx2` → `search_avx2_find` |
| 函数调用开销 | ~20-40 cy/q | 可通过 inline 消除 |

### 7.3 排除了哪些假说

| 假说 | 验证结果 |
|------|----------|
| ❌ popcount 回退到软件模拟 | 汇编确认为硬件 `popcntl` |
| ❌ CPU 检测假阴性（走了标量路径） | `platform_cpu_has_avx2()` = yes |
| ❌ 预热不足导致 Turbo 偏差 | 已用 ~500ms 长预热消除 |
| ❌ 计时精度不足 | 已用 `__rdtscp()` + `_mm_lfence()` |
| ❌ 编译器差异 | 仅 Windows GCC 15.2.0 测试，需第四波 Linux 对比 |

---

## 8. 建议

### 8.1 短期（Phase 1 收尾）

| 优先级 | 行动 | 说明 |
|--------|------|------|
| P1 | 标记为已知限制 | 在 FINAL/ACCEPTANCE 文档中注明：AVX2 在大规模数据（>10M）或特定 CPU 上才有效 |
| P1 | 默认使用标量路径 | 对于 ≤10M 数据，`platform_cpu_has_avx2()` 返回 false 或调整调度策略 |
| P2 | 第四波 Linux 对比 | 在 Linux 上确认是否是 Windows 特有现象（见 FIXPLAN 第四波） |

### 8.2 中期（Phase 2+）

| 优先级 | 行动 | 说明 |
|--------|------|------|
| P2 | 探索 AVX-512 路径 | AVX-512 的 16-wide 比较可能改变性价比 |
| P2 | 探索 SVE (ARM) 路径 | 不同 SIMD 宽度下的性价比分析 |
| P3 | 自适应策略 | 根据数据大小 N 自动选择 AVX2/标量路径 |

### 8.3 长期

AVX2 SIMD 在二分搜索中的合理使用场景：
- N >> 100M，cache miss 占主导
- 批处理查询（向量化查询而非向量化单步）
- 使用 gather/scatter 指令的索引搜索算法

---

## 9. 调查结论汇总

> **编译选项分析缺口**（meeting_005 D-040）：本报告初稿（2026-05-29）未覆盖 `-march=native` 对比实验和 POC 代码公平对比。
> 2026-05-30 补全：TODO-01 E1-E4 四项实验已完成（见 [05_todo01_benchmark_report.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/05_todo01_benchmark_report.md)），
> 双平台确认根因为算法效率瓶颈，编译器选项和代码差异均已排除。

| 编号 | 任务 | 状态 | 发现 |
|------|------|------|------|
| INVEST-01 | popcount 指令验证 | ✅ | 硬件 `popcntl`，无误 |
| INVEST-02 | CPU 检测确认 | ✅ | AVX2=yes，非假阴性 |
| INVEST-03 | popcount 修复 | ⏭️ 跳过 | 无需修复 |
| INVEST-04 | 重新 benchmark | ✅ | 改造后公平对照完成 |
| INVEST-05 | 4 组公平对照 | ✅ | API/Raw AVX2/Raw Scalar/Inline |
| INVEST-06 | 计时改进 | ✅ | `__rdtscp()` + `_mm_lfence()` |
| INVEST-07 | 长预热 | ✅ | ~500ms 循环预热 |
| INVEST-08 | CPU 信息打印 | ✅ | CPUID + AVX2 检测状态 |

**最终结论**: AVX2 异常不是编译器/指令选择 bug，而是**算法效率**问题。在当前 CPU 和数据规模下，AVX2 SIMD 辅助二分搜索的额外迭代开销超过迭代减少带来的收益。推荐在 Phase 1 中将此标记为已知限制，Phase 2 探索 AVX-512 或自适应调度策略。

---

> 本调查完成。

---

## 归档元数据

| 字段 | 值 |
|------|-----|
| 原始路径 | docs/tasks/task_001_phase1_mvp/INVESTIGATION_windows_avx2_task_001_phase1_mvp.md |
| 归档日期 | 2026-05-30 |
| 归档版本 | meeting_005/006 Reviewed (INVEST-01~08 全部完成) |
| task_id | task_001_phase1_mvp |
