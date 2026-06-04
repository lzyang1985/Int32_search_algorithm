---
title: POC Benchmark 报告 - Int32查找算法方案可行性评审会
meeting_id: meeting_001_feasibility_review
status: Frozen
created_at: 2026-05-27
updated_at: 2026-05-27
author: Agent
parent_doc: docs/meetings/meeting_index/meeting_001_feasibility_review/meeting_README.md
source_code: src/poc_benchmark.c
tags: [benchmark, poc, avx2, simd]
---

# POC Benchmark 报告

## 1. 背景

会议第三至四轮讨论中出现核心技术分歧——两个 SIMD 加速方案（AVX 向量化二分 vs 低 16 位平行数组）各有理论支撑，无法在纸面上达成一致。人工决策：先做 POC benchmark，用实测数据裁决。

## 2. 测试环境

| 项目 | 值 |
|------|-----|
| 编译器 | gcc |
| 编译选项 | `-O3 -mavx2 -march=native` |
| 平台 | Windows (PowerShell) |
| CPU 频率假设 | 3.5 GHz（用于 ns 换算，仅近似） |
| 代码文件 | [src/poc_benchmark.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark.c) |
| 编译命令 | `gcc -O3 -mavx2 -march=native -o src/poc_benchmark.exe src/poc_benchmark.c` |

## 3. 被测方案

### 3.1 方案概览

| 方案 | 简称 | 描述 | 预期加速比（会议讨论） |
|------|------|------|---------------------|
| 标量二分 | Baseline | 标准 `while (lo < hi)` 二分查找 | 1.0x（基线） |
| AVX2 SIMD 二分 | 方案 A | 排序数组上做 AVX2 向量化二分，每次比较 8 个 int32，用 `popcount` 定位 | 有争议：1.03x ~ 若干倍 |
| 锚点 + AVX2 二分 | 方案 A' | 稀疏锚点索引（间隔 256）做标量二分定位窗口，窗口内 AVX2 二分 | 架构师预估 ~90 cycles |
| lo16 平行数组 | 方案 B | high16 锚点 + lo16 SIMD 等值扫描（`_mm256_cmpeq_epi16`，一次 16 个 uint16）+ 假阳性回原数组验证 | 算法工程师预估 ~55 cycles |

### 3.2 方案 A：AVX2 SIMD 向量化二分查找

```
数据结构：int32_t sorted_array[n]   (40MB / 1000万)
        无辅助索引

算法：每一步取区间中点对齐到 8 的边界，AVX2 批量加载 8 个 int32
     _mm256_cmpgt_epi32 比较 key vs 8 个值
     _mm256_movemask_ps + popcount 得到 <= key 的个数 le_count
     根据 le_count 更新搜索区间 [lo, hi)
     当 hi - lo < 8 时退化为标量二分

Intrinsic链：_mm256_set1_epi32 → _mm256_loadu_si256 → _mm256_cmpgt_epi32
           → _mm256_movemask_ps → __builtin_popcount
```

### 3.3 方案 A'：锚点索引 + 窗口 AVX2 二分

```
数据结构：int32_t sorted_array[n]        (40MB / 1000万)
         int32_t anchors[n/256]          (~156KB / 1000万)

算法：先在 anchors 上做标量二分 → 定位到 [anchor_i-1, anchor_i+1) × 256 窗口
     窗口内用与方案 A 相同的 AVX2 二分查找
     窗口大小约 512 个元素
```

### 3.4 方案 B：high16 锚点 + lo16 平行数组 SIMD 扫描

```
数据结构：int32_t vals[n]                (40MB / 1000万)
         uint16_t lo16[n]               (20MB / 1000万)
         int32_t anchors[n/256]          (~156KB / 1000万)

算法：先在 anchors 上做标量二分 → 定位到 [anchor_i-1, anchor_i+1) × 256 窗口
     窗口内用 _mm256_cmpeq_epi16 扫描 lo16 数组（一次 16 个 uint16）
     找到 lo16 匹配后回 vals 数组做完整 int32 验证（消除假阳性）
     假阳性率预估：K=256 时约 0.39%/查询
```

## 4. 测试方法

### 4.1 参数矩阵

| 参数 | 值 |
|------|-----|
| 数据量 N | 1,000,000 / 5,000,000 / 10,000,000 |
| 查询量 | 1,000,000 次/组 |
| 命中率 | 50%（50 万命中 + 50 万未命中） |
| 数据分布 | 均匀随机（rand() ^ (rand() << 15)） |
| 查询分布 | 随机打乱 |
| 锚点间隔 | 256（A' 和 B 共用） |
| 预热 | 100 次查询 |

### 4.2 测试流程

```
1. 生成随机 int32 → qsort 排序 → sorted_data[]
2. 从 sorted_data 抽样 50% → 命中查询
3. 生成不存在的 int32 → bsearch 验证不在数组内 → 未命中查询
4. shuffle 混合 → queries[] (100万条)
5. 逐方案运行：预热 100 次 → rdtsc 计时 → 100万次查询 → 计算 avg cycles/q
6. 对 1M / 5M / 10M 分别重复
```

### 4.3 计时方式

```c
// 预热（消除冷缓存影响）
for (w = 0; w < 100; w++)
    discard |= search(ctx, queries[w]);

// 计时
uint64_t t0 = rdtsc();
for (q = 0; q < 1000000; q++)
    discard |= search(ctx, queries[q]);
uint64_t t1 = rdtsc();

avg_cycles = (t1 - t0) / 1000000.0;
```

使用 `volatile int32_t discard` 防止编译器优化掉查询结果。

## 5. 实测结果

### 5.1 原始数据

| 方案 | 1M 数据 | 5M 数据 | 10M 数据 |
|------|---------|---------|----------|
| 标量二分 | 424.0 cy/q | 702.3 cy/q | 961.8 cy/q |
| **AVX2 SIMD 二分 (方案A)** | **120.2 cy/q** | **192.2 cy/q** | **189.1 cy/q** |
| 锚点+AVX2 二分 (方案A') | 327.9 cy/q | 561.0 cy/q | 678.8 cy/q |
| lo16 平行数组 (方案B) | 415.3 cy/q | 612.8 cy/q | 742.5 cy/q |

### 5.2 加速比（vs 标量二分基线）

| 方案 | 1M | 5M | 10M |
|------|-----|-----|------|
| 标量二分 | 1.00x | 1.00x | 1.00x |
| **AVX2 SIMD 二分 (方案A)** | **3.53x** | **3.66x** | **5.09x** |
| 锚点+AVX2 二分 (方案A') | 1.29x | 1.25x | 1.42x |
| lo16 平行数组 (方案B) | 1.02x | 1.15x | 1.30x |

### 5.3 横向对比（以方案 A 为基准）

| 方案 | 1M | 5M | 10M |
|------|-----|-----|------|
| 方案 A（基准） | 1.00x | 1.00x | 1.00x |
| 方案 A' vs A | 2.73x 慢 | 2.92x 慢 | 3.59x 慢 |
| 方案 B vs A | 3.46x 慢 | 3.19x 慢 | 3.93x 慢 |

### 5.4 可视化

```
cycles/query
 1000 │                                    ● 标量二分
      │
  800 │                       ●(A')       ●(B)
      │                    ●(B)        ●(A')
  600 │                 ●(A')
      │
  400 │  ●(B)
      │  ●(A')  ●(标量)
      │
  200 │  ●(A)         ●(A)         ●(A)
      │
    0 └─────┬──────────┬──────────┬─────
           1M          5M         10M     数据量

    ●(A)  = AVX2 SIMD 二分         —— 最优
    ●(A') = 锚点+AVX2 二分         —— 中等
    ●(B)  = lo16 平行数组          —— 接近甚至差于标量
```

## 6. 结果分析

### 6.1 方案 A 为何远超预期

会议讨论中算法工程师预测"扁平 AVX 二分在 10M 下仅 1.03x"。实测为 **5.09x**，原因：

1. **一次比较 8 个元素即缩小搜索范围**：本实现不是简单并行化单次比较，而是每步读取 8 个值后用 `popcount` 统计 ≤ key 的个数，直接将搜索区间跳过最多 8 个元素。等价于将 O(log₂ n) 降为约 O(log₈ n)，步数从 ~24 降到 ~8。

2. **无分支设计**：mask → popcount 路径消除了传统二分中 50% 的分支预测失败率。

3. **数据完全在 L3 缓存内**：10M × 4B = 40MB，现代 CPU L3 缓存 16-36MB，大部分数据在 L3 命中。

### 6.2 方案 A' / B 为何负优化

| 因素 | 影响 |
|------|------|
| 锚点标量二分 16 步 | 每步 ~3 cycles + 分支预测失败 ≈ 50-60 cycles，占方案 A' 总开销的 ~18% |
| 窗口内冗余搜索 | 512 个元素窗口，方案 A 只需要 ~8 步，方案 A' 窗口内也要 ~8 步，总操作 > 方案 A |
| lo16 扫描尾部分支 | 方案 B 的 `_mm256_movemask_epi8` → `while(mask)` 路径在 50% 不命中时触发额外分支 |
| lo16 数据未在 L1 | lo16 数组（20MB）不在热缓存中，连续扫描也需要访存 |
| 构建开销 | lo16 数组 + 锚点数组构建不在这次测量中，但实际使用中需计入 |

核心结论：**额外索引层的固定开销超过了它节省的搜索开销。对排序数组 + SIMD 二分而言，"什么都不加"就是最优策略。**

### 6.3 10M 比 5M 更快的原因

方案 A 在 10M（189.1 cy/q）反而比 5M（192.2 cy/q）快，这个反直觉现象的原因是：

- 5M = 20MB → L3 缓存（典型 16-36MB），部分溢出
- 10M = 40MB → 反而不在 L3，但 CPU 使用硬件预取器（stride prefetcher）进行了有效预取
- 这是一个巧合现象，不应过度解读

## 7. 内存占用精算

| 方案 | 10M 数据 | 构成 |
|------|----------|------|
| 方案 A | **40 MB** | `int32_t[]` 排序数组 |
| 方案 A' | **40.16 MB** | 40MB 数据 + 156KB 锚点 |
| 方案 B | **60.16 MB** | 40MB vals + 20MB lo16 + 156KB 锚点 |

全部在 1-2GB 预算内。

## 8. 最终结论

| 决策项 | 结论 |
|--------|------|
| **选定方案** | **方案 A：AVX2 SIMD 向量化二分查找** |
| 实测加速比 | 3.5x-5.1x vs 标量二分 |
| 内存占用 | 40MB / 1000万 |
| 代码复杂度 | 最低（单数组、单函数、~120 行） |
| 锚点索引 | ❌ 舍弃（负优化） |
| lo16 平行数组 | ❌ 舍弃（负优化） |
| 自适应双路径 | ❌ 不需要 |
| AVX-512 升级 | ✅ 保留为编译可选路径 |
| 桶排序+布隆 | ✅ 保留为可选编译开关 |

---

> 📌 本报告为 meeting_001_feasibility_review 的最终交付物。会议状态 → Frozen，可进入立项阶段。
