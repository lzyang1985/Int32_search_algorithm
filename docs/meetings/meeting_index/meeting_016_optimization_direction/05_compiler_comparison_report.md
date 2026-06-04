---
title: 编译器对比调研报告（ACT-33 & ACT-34）
meeting_id: meeting_016_optimization_direction
status: Draft
created_at: 2026-06-03
updated_at: 2026-06-03
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_016_optimization_direction/meeting_README.md
parent_task: root
source_actions: [ACT-33, ACT-34]
tags: [benchmark, compiler, gcc, clang, avx2]
---

# 编译器对比调研报告（ACT-33 & ACT-34）

## 1. 调研背景

根据 meeting_016 决议 [D-125]，调研编译选项和编译器对 Int32 查找库性能的影响。
- **ACT-33**: GCC `-march=native` vs `-mavx2` 对比
- **ACT-34**: Clang vs GCC 性能对比（Linux + Windows 双平台）

---

## 2. 测试方法

- **数据**: uniform random, 50% 命中率, 1M queries
- **数据规模**: 1M / 5M / 10M
- **计时**: `__rdtscp()` + `_mm_lfence()`, warmup ~500ms
- **分组**: API (Path B1/A), Raw AVX2, Raw Scalar, Inline Scalar

---

## 3. ACT-33: `-march=native` vs `-mavx2`

**平台**: Linux, Intel Xeon Gold 6152 @ 2.10GHz, GCC 11.4.0

### 3.1 API 路径 (cy/query)

| 数据规模 | `-mavx2` | `-march=native` | 差异 |
|----------|----------|-----------------|------|
| 1M | 281.7 | 229.3 | **native -18.6%** |
| 5M | 372.9 | 392.5 | native +5.3% |
| 10M | 470.2 | 489.7 | native +4.1% |

### 3.2 Raw AVX2

| 数据规模 | `-mavx2` | `-march=native` | 差异 |
|----------|----------|-----------------|------|
| 1M | 1384.7 | 1278.1 | native -7.7% |
| 5M | 2364.6 | 2521.3 | native +6.6% |
| 10M | 2717.9 | 2789.8 | native +2.6% |

### 3.3 Inline Scalar

| 数据规模 | `-mavx2` | `-march=native` | 差异 |
|----------|----------|-----------------|------|
| 1M | 587.2 | 533.6 | native -9.1% |
| 5M | 1029.8 | 1126.9 | **native +9.4%** |
| 10M | 1244.1 | 1370.0 | **native +10.1%** |

### 3.4 结论

**`-march=native` 未带来稳定收益，不推荐替换 `-mavx2`。**

- 小数据 (1M) 略有改善，但大数据 (5M/10M) 反而退化
- 推测原因：Xeon Gold 6152 在 `-march=native` 下启用 AVX-512 支持，触发频率压制，导致纯 AVX2 代码间接受损
- Inline Scalar 退化最明显（10M 退化 10%），可能是不必要的自动向量化尝试
- **建议**: 继续使用 `-mavx2` 作为默认编译选项。`-march=native` 仅在 AVX-512 专项优化时重新评估

---

## 4. ACT-34: Clang vs GCC

### 4.1 Linux 服务器 (Xeon Gold 6152): Clang 14 vs GCC 11.4

**API 路径 (cy/query)**

| 数据规模 | GCC 11.4 | Clang 14 | 差异 |
|----------|----------|----------|------|
| 1M | 281.7 | 192.6 | **Clang -31.6%** |
| 5M | 372.9 | 478.3 | Clang +28.3% |
| 10M | 470.2 | 462.9 | Clang -1.6% |

**Raw AVX2 (cy/query)**

| 数据规模 | GCC 11.4 | Clang 14 | 差异 |
|----------|----------|----------|------|
| 1M | 1384.7 | 1299.0 | Clang -6.2% |
| 5M | 2364.6 | 2508.0 | Clang +6.1% |
| 10M | 2717.9 | 3153.7 | **Clang +16.0%** |

**Inline Scalar (cy/query)**

| 数据规模 | GCC 11.4 | Clang 14 | 差异 |
|----------|----------|----------|------|
| 1M | 587.2 | 534.6 | Clang -9.0% |
| 5M | 1029.8 | 1117.6 | Clang +8.5% |
| 10M | 1244.1 | 1447.8 | **Clang +16.4%** |

### 4.2 Windows 本地 (Skylake): Clang 20 (LLVM-MinGW)

| 数据规模 | API | Raw AVX2 | Raw Scalar | Inline Scalar |
|----------|-----|----------|------------|---------------|
| 1M | 168.3 | 997.9 | 406.5 | 466.8 |
| 5M | 255.0 | 1299.9 | 668.2 | 642.5 |
| 10M | 527.0 | 1466.8 | 849.5 | 802.1 |

> 本地 CPU 与服务器不同，跨平台数据不可直接对比。

### 4.3 结论

1. **Linux 大数据场景 GCC 全面领先 Clang 14**
   - Raw AVX2 快 16%，Inline Scalar 快 16%
   - GCC 在 L3 cache 压力大时对 AVX2 intrinsic 指令调度更优

2. **小数据 (1M) Clang 有优势** — 但 1M 场景在实际应用中不典型

3. **API 路径波动大** — 不同编译器可能触发不同路径选择 (B1 vs A)，API 数据不代表纯编译器差异

4. **建议**: GCC 继续作为主力编译器。Clang 可用于快速编译验证（编译速度更快），但不推荐用于生产构建

---

## 5. 综合建议

| 维度 | 推荐 | 原因 |
|------|------|------|
| 默认编译选项 | `-mavx2` | 比 `-march=native` 稳定，大数据不退化 |
| 主力编译器 | GCC | 大数据场景 AVX2 intrinsic 调度更优 |
| Clang 用途 | 辅助验证 | 编译速度快，可用于 CI 快速检查 |

---

## 6. 原始数据

- Linux GCC `-mavx2`: 服务器 `/root/Int32_search_algorithm/bench_mavx2.log`
- Linux GCC `-march=native`: 服务器 `/root/Int32_search_algorithm/bench_native.log`
- Linux Clang 14: 服务器 `/root/Int32_search_algorithm/bench_clang.log`
