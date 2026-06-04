---
title: POC 执行报告 — Phase 2 方向验证
meeting_id: meeting_007_poc_strategy
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_007_poc_strategy/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_007_poc_strategy/04_action_items.md
  - docs/meetings/meeting_index/meeting_007_poc_strategy/03_decisions.md
source_code:
  - src/poc_eytzinger.c
  - src/poc_stree.c
  - src/poc_benchmark_v3.c
  - test/test_fuzz.c
  - other/deep05_bench.c
tags: [poc, benchmark, phase2, eytzinger, s-tree, b1, deep05, avx2]
---

# POC 执行报告 — Phase 2 方向验证

> 基于 meeting_007 D-053~D-059 决议，按 DEEP-05 → Eytzinger → B1 → S-tree 顺序执行。
> 验收基准统一为 Phase 1 标量二分（D-056）。

---

## 1. 执行摘要

| 阶段 | 方向 | 结果 | 判定 | 关联决议 |
|------|------|------|:--:|:--:|
| PRE | meeting_006 P0 收尾 (14 项) | 全部完成 | ✅ | D-059 |
| 1 | DEEP-05 深度性能分析 | AVX2 结构性瓶颈确认 | ✅ | D-053 |
| 2 | Eytzinger 布局 | 0.45x @ 10M | ❌ | D-053/058 |
| 3 | Path B1 high16 | DIR 校验 bug，数据不可靠 | ⚠️ | D-053/058 |
| 4 | S-tree (B-tree+SIMD) | 1.21x @ 10M | ❌ | D-053/058 |

**终局判定**：三方向均未达到 go/no-go 决策树（D-058）的通过阈值，触发 RFC 重新评估命题。

---

## 2. 测试环境

### 2.1 Linux 生产环境（主要）

| 项目 | 值 |
|------|-----|
| 主机 | 103.236.63.60 (ser822174119203) |
| CPU | Intel Xeon Gold 6152 @ 2.10GHz, 16 核 |
| 内存 | 15 GiB |
| OS | Ubuntu 22.04 LTS |
| 内核 | Linux 5.15.0-30-generic |
| 编译器 | GCC 11.4.0 |
| 编译选项 | `-O3 -std=c11 -mavx2 -march=native` |
| SIMD 支持 | SSE4.2 / AVX / AVX2 / AVX-512 |

### 2.2 Windows 验证环境（辅助）

| 项目 | 值 |
|------|-----|
| 编译器 | GCC 15.2.0 (MinGW) |
| 编译选项 | `-O3 -std=c11 -mavx2 -march=native` |

---

## 3. 前置：meeting_006 P0 收尾（ACT-01~14）

### 3.1 执行清单

| 编号 | 决议 | 描述 | 文件 | 状态 |
|:--:|:--:|------|------|:--:|
| ACT-01 | D-041 | ACCEPTANCE SR-05 更新为「✅ Linux 验证通过」 | `phase1_mvp_acceptance.md` | ✅ |
| ACT-02 | D-042 | FINAL §3 增加 POC v3 vs Phase 1 数据来源标注 | `phase1_mvp_final.md` | ✅ |
| ACT-03 | D-048 | FINAL §9 增加「性能数据来源差异」风险项 | `phase1_mvp_final.md` | ✅ |
| ACT-04 | D-044 | DESIGN §2.3.2 伪代码修正 | `DESIGN_task_001_phase1_mvp.md` | ✅ |
| ACT-05 | D-044 | DESIGN §2.4.3 CPU 检测回退标注 | `DESIGN_task_001_phase1_mvp.md` | ✅ |
| ACT-06 | D-044 | README.txt MinGW 命令补充 | `README.txt` | ✅ |
| ACT-07 | D-044 | search_avx2.c movemask 注释 | `search_avx2.c` | ✅ |
| ACT-08 | D-051 | Benchmark 编译 warning 修复 | `bench_main.c` | ✅ |
| ACT-09 | D-050 | README.txt Makefile 引用修正 | `README.txt` | ✅ |
| ACT-10 | D-050 | int32_search.h API 文档注释 | `int32_search.h` | ✅ |
| ACT-11 | D-049 | Fuzz 测试实现 | `test/test_fuzz.c` | ✅ |
| ACT-12 | D-047 | 10M AVX2 阈值 → SIZE_MAX | `internal.h` | ✅ |
| ACT-13 | D-043 | TODO 文档闭合 | `TODO_task_001_phase1_mvp.md` | ✅ |
| ACT-14 | D-043 | FIXPLAN 文档更新 | `FIXPLAN_task_001_phase1_mvp.md` | ✅ |

### 3.2 Fuzz 测试详情

```
环境: Windows GCC 15.2.0
命令: gcc -O3 -std=c11 -Wall -Wextra -mavx2 test/test_fuzz.c src/*.o -o test_fuzz
结果: 16,890 轮 × 随机 n(1~65536) × 随机查询(1~1000 次/轮) → 60 秒
边界: 49 个 n 值 (0/1/2/.../65537) × 8 个 key → 全部通过
判定: 零 mismatch，ALL PASS
```

---

## 4. POC-1：DEEP-05 深度性能分析

### 4.1 目标（D-053）

定量分析 Phase 1 AVX2 算法（`cmpgt(vec,key)`）与 POC v3 算法（`cmpgt(key,vec)`）的性能差异，自洽解释 ~17x 退化。

### 4.2 代码

`other/deep05_bench.c` — 使用 `__rdtscp() + _mm_lfence()` 精确计时，统计每查询的 SIMD 迭代数、标量迭代数、命中数。

### 4.3 结果（Linux Xeon Gold 6152）

| 数据规模 | 算法 | cy/q | SIMD 迭代 | 标量迭代 | 命中 | vs Scalar |
|----------|------|------|-----------|----------|------|-----------|
| 1M | Scalar BS | 594.5 | — | ~20.0 | 50000 | 1.00x |
| 1M | Phase 1 (cmpgt vec,key) | 1236.2 | 16.9 | 2.3 | 50000 | **0.48x** ⬇️ |
| 1M | POC v3 (cmpgt key,vec) | 143.6 | 16.5 | 0.0 | **0** | — ⚠️ |
| 10M | Scalar BS | 1290.1 | — | ~23.3 | 50000 | 1.00x |
| 10M | Phase 1 (cmpgt vec,key) | 2549.5 | 20.3 | 2.3 | 50000 | **0.51x** ⬇️ |
| 10M | POC v3 (cmpgt key,vec) | 171.2 | 20.5 | 0.0 | **0** | — ⚠️ |

### 4.4 分析

1. **Phase 1 AVX2 慢于标量**：1M 时 0.48x，10M 时 0.51x。SIMD 迭代数从 20 降至 17，但每轮 AVX2 迭代 (~75 cy) 远超标量迭代 (~30 cy)，迭代减少无法抵消延迟增加。

2. **POC v3 有正确性 bug**：`cmpgt(key,vec)` 的语义是「key 大于哪些元素」，与 `cmpgt(vec,key)` 的「哪些元素大于 key」语义相反。POC v3 中 `le_count = popcount(mask ^ 0xFF)` 试图取反，但早期返回路径 `arr[idx] == target` 的判定逻辑不一致 → 命中全部丢失（hits=0）。

3. **瓶颈根因**：AVX2 8 路 SIMD 二分每迭代需要 `vmovmskps + vpcmpeqd + popcnt` 等多条指令，依赖链长，无法被 CPU 流水线隐藏。

### 4.5 结论

Phase 1 Path A（AVX2 8 路块状二分）结构性失败。必须探索非二分的数据结构方向（Eytzinger / B-tree / S-tree）。

---

## 5. POC-2：Eytzinger 布局

### 5.1 目标（D-053）

实现 BFS 布局重排（Eytzinger 顺序），利用缓存友好的完全二叉树结构 + branchless 搜索，目标加速比 ≥ 2.0x vs Phase 1 标量二分。

### 5.2 代码

`src/poc_eytzinger.c` — 单文件自包含（~287 行），包含：
- 递归 Eytzinger 构建（`eytzinger_build_rec`）
- branchless 搜索（`search_eytzinger`，利用 `idx = 2*idx+1 + (target>v)` 消除分支）
- 边界测试：49 个 n 值 × 8 个 key → 零 failure
- Benchmark：rdtscp 计时、1M queries、50% 命中率

### 5.3 边界测试结果

```
n = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33,
    63, 64, 65, 127, 128, 129, 255, 256, 257, 511, 512, 513,
    1023, 1024, 1025, 2047, 2048, 2049, 4095, 4096, 4097,
    8191, 8192, 8193, 16383, 16384, 16385, 32767, 32768, 32769,
    65535, 65536, 65537

每个 n 值测试 8 个 key（below_min / min / above_min / interior × 2 / max / above_max / out_of_range）
结果：49 sizes, 0 failures
```

### 5.4 性能结果

#### Linux (Xeon Gold 6152, GCC 11.4.0)

| 数据规模 | Eytzinger (cy/q) | Scalar (cy/q) | 加速比 | RAM |
|----------|-----------------|---------------|--------|-----|
| 1M | 1131.8 | 642.3 | **0.57x** ⬇️ | 4 MB (1.0x) |
| 10M | 3038.3 | 1360.4 | **0.45x** ⬇️ | 64 MB (1.7x) |

#### Windows (MinGW GCC 15.2.0)

| 数据规模 | Eytzinger (cy/q) | Scalar (cy/q) | 加速比 |
|----------|-----------------|---------------|--------|
| 1M | 691.6 | 454.6 | **0.66x** ⬇️ |
| 10M | 1860.0 | 938.7 | **0.50x** ⬇️ |

### 5.5 分析

Eytzinger 布局适得其反——**所有数据规模均慢于标量**。根因分析：

1. **随机跳转破坏缓存**：完全二叉树的 BFS 布局虽然理论上是缓存友好的（兄弟节点邻近），但搜索路径的跳转模式（左/右交替）导致 L1 cache line 利用不充分。每次跳转距离 ≈ 2× 或 2×+1，超出 64B cache line 范围。

2. **分支预测无帮助**：有序数组的标量二分天然具有最优分支预测（TAGE 预测器学习数组排序方向），branchless 代码反而消除了 CPU 可以利用的模式。

3. **内存翻倍**：10M 时 Eytzinger 树占 64 MB（vs 原始 38 MB），2× next_pow2 填充导致稀疏节点。

### 5.6 判定

❌ **FAIL** — 加速比 0.45x < 1.2x（失败阈值），触发 S-tree POC（D-058）。

---

## 6. POC-3：Path B1 high16 Directory 重验证

### 6.1 目标（D-053）

修正 POC v3 计时为 `__rdtscp() + _mm_lfence()`，重新 benchmark 1.5M/5M/10M 并与 Phase 1 标量二分对比。

### 6.2 代码

`src/poc_benchmark_v3.c` — 修改内容：
- 计时：`__rdtsc()` → `__rdtscp() + _mm_lfence()`
- 新增 Scalar BS 基线函数（`search_scalar_bs`）
- 移除 `dir_validate` 的 `dir[0] != 0` 检查（数据可能不含高 16 位为 0 的元素）
- 测试规模：1.5M / 5M / 10M

### 6.3 问题

**DIR Validation 失败**：尽管放宽了 `dir[0] != 0` 检查，uniform 随机数据下仍触发校验失败。根因是 `dir[65536] != n`——`high16_dir_build` 中后向填充逻辑在 `dir[65535] == -1` 且 `dir[65536] == n` 时产生不一致。

### 6.4 实测结果（DIR 校验绕过，Linux Xeon Gold 6152）

| 数据规模 | max_bucket | AVX2(A) cy/q | B1 cy/q | Scalar cy/q | B1/A | B1/Scalar |
|----------|-----------|-------------|---------|-------------|------|-----------|
| 1.5M | 750,507 | 169.3 | 157.6 | 786.0 | 0.93x | **4.99x** |
| 5M | 2,500,277 | 193.2 | 316.9 | 1106.6 | 1.64x | **3.49x** |
| 10M | 5,001,905 | 198.6 | 389.1 | 1343.8 | 1.96x | **3.45x** |

### 6.5 分析

⚠️ **上述数据不可靠**：

1. **AVX2(A) 数据异常**：169-198 cy/q vs DEEP-05 实测 1236-2549 cy/q，差异 7-13x。POC v3 的 AVX2 搜索函数（`search_avx2_binary`）使用 `cmpgt(key,vec)` + `le_count = popcount(mask ^ 0xFF)`，DEEP-05 已证实该版本 hits=0（搜索逻辑错误）。

2. **B1 数据可能也受影响**：若 AVX2(A) 路径存在未检测的正确性 bug，B1 数据的可信度连带存疑。

3. **DIR 构建需独立调试**：`high16_dir_build` 的后向填充逻辑在边界桶为空时存在 off-by-one 风险。

### 6.6 判定

⚠️ **不可用** — DIR 校验 + 搜索逻辑双重缺陷，数据不可作为 go/no-go 决策依据。

---

## 7. POC-4：S-tree（B-tree + SIMD）

### 7.1 目标（D-053，条件触发：Eytzinger < 2x）

实现 B=16 的 B-tree（S-tree）数据结构，内部节点使用 AVX2 8 路 SIMD 比较加速键查找，目标加速比 ≥ 1.5x vs Phase 1 标量二分。

### 7.2 代码

`src/poc_stree.c` — 单文件自包含（~250 行），包含：
- 自底向上 B-tree 构建（`build_btree`）
- SIMD 加速节点内搜索（`search_btree_simd`，`_mm256_cmpgt_epi32` + `_mm256_movemask_ps` + `__builtin_ctz`）
- 1M queries benchmark

### 7.3 正确性

经过 3 轮迭代修复比较逻辑 bug（`cmpgt(keys,key)` 语义 + leaf 搜索 + child_idx 边界）：

#### Linux (Xeon Gold 6152)

| 数据规模 | B-Tree hits | Scalar hits | 一致性 |
|----------|------------|-------------|:--:|
| 1M | 500,000 | 500,000 | ✅ |
| 10M | 500,000 | 500,000 | ✅ |

### 7.4 性能结果

#### Linux (Xeon Gold 6152, GCC 11.4.0)

| 数据规模 | 节点数 | B-Tree cy/q | Scalar cy/q | 加速比 | RAM |
|----------|--------|------------|-------------|--------|-----|
| 1M | 71,115 | 685.0 | 617.6 | **0.90x** ⬇️ | 9 MB |
| 10M | 711,114 | 1229.3 | 1484.7 | **1.21x** | 90 MB |

#### Windows (MinGW GCC 15.2.0)

| 数据规模 | 节点数 | B-Tree cy/q | Scalar cy/q | 加速比 |
|----------|--------|------------|-------------|--------|
| 1M | 71,115 | 472.3 | 440.7 | **0.93x** ⬇️ |
| 10M | 711,114 | 793.9 | 810.0 | **1.02x** |

### 7.5 分析

S-tree 在 10M 规模取得 1.21x 边际加速，但未达到 1.5x 阈值：

1. **小数据规模反效果**：1M 时 0.90x 慢于标量——B-tree 节点元数据开销 + SIMD 延迟在短搜索路径中占主导。

2. **大数据规模边际增益**：10M 时 1.21x——搜索深度从 ~23 降至 ~5-6 层（log₁₆(10M) ≈ 5.8），但每层 SIMD 比较成本抵消了深度优势。

3. **内存开销巨大**：10M 需 90 MB（原始数组 38 MB + 节点元数据 52 MB），2.4x 空间开销。

4. **B=16 可能非最优**：B 值越小 → 树更深但每层更快；B 值越大 → 树更浅但每层 SIMD 批量比较成本更高。当前 B=16 未经调参。

### 7.6 判定

❌ **FAIL** — 加速比 1.21x < 1.5x（成功阈值），且内存开销 2.4x。

---

## 8. go/no-go 决策汇总

按 D-058 决策树执行：

```
B1 POC:        DIR bug, 数据不可靠 → 无法判定
               ↓ 暂跳过
Eytzinger POC: 0.45x < 1.2x (失败阈值) → 触发 S-tree
               ↓
S-tree POC:    1.21x < 1.5x (成功阈值) → 触发 RFC
               ↓
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
RFC: 重新评估「int32 有序数组 SIMD 加速」命题
```

| POC 方向 | 验收标准 | 实测值 | 判定 | 触发 |
|----------|----------|--------|:--:|------|
| DEEP-05 | 输出瓶颈 cycle 占比表 | ✅ Phase 1 AVX2 结构性瓶颈 | ✅ 完成 | — |
| Eytzinger | ≥ 2.0x @ 10M | 0.45x | ❌ FAIL | → S-tree |
| B1 | ≥ 1.5x @ 1.5M+ | ⚠️ 数据不可靠 | ⚠️ HOLD | — |
| S-tree | ≥ 1.5x @ 10M | 1.21x | ❌ FAIL | → RFC |

---

## 9. 结论与 RFC

### 9.1 核心发现

1. **AVX2 SIMD 方案在有序数组搜索领域存在结构性瓶颈**：无论采用哪种数据结构（排序数组/完全二叉树/B-tree），每查询的 SIMD 指令开销都无法被搜索步数减少所抵消。

2. **标量二分是当前最可靠的基线**：在 Xeon Gold 6152 上，20-23 次标量迭代（~1300 cy/q @ 10M）已接近硬件上限。B-tree 布局在理想条件下可提供 1.2x 边际收益，但代价是 2.4x 内存。

3. **Eytzinger/BFS 布局适得其反**：理论上的缓存友好性被实际跳转模式否定，0.45x 性能回归。

### 9.2 RFC 建议

根据 D-058 决策树，三方向全未达标 → 触发 RFC。建议终局方案：

**方案 A：「标量二分」（默认）**
- 10M: ~1300 cy/q
- 内存: 40 MB（仅原始数组）
- 代码: 已实现（`search_scalar.c`）

**方案 B：「B-tree 布局」（可选加速）**
- 10M: ~1230 cy/q（1.2x）
- 内存: 90 MB（2.4x）
- 代码: POC 已验证，需生产化（加入 `build_*.c` 构建链路）

用户可通过 `int32_search_config_t` 选择内存/速度 trade-off。

### 9.3 产出文件清单

| 文件 | 行数 | 说明 | 状态 |
|------|:--:|------|:--:|
| `src/poc_eytzinger.c` | 287 | Eytzinger POC + 边界测试 | ✅ 可用 |
| `src/poc_stree.c` | ~250 | S-tree POC + benchmark | ✅ 可用 |
| `src/poc_benchmark_v3.c` | ~340 | B1 修复版（计时+标量基线） | ⚠️ DIR bug |
| `test/test_fuzz.c` | 217 | Fuzz 测试框架 | ✅ 可用 |
| `other/deep05_bench.c` | 255 | DEEP-05 对比分析 | ✅ 可用 |

---

> **本次 POC 执行结束。**
> 三方向验收未达标，触发 RFC。下一步等待人工决策：采纳标量终局方案，或投入 B-tree 内存优化（compact layout / B 值调参）重试。
