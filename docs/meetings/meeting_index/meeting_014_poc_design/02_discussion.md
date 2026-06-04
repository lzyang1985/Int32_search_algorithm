---
title: 讨论记录 — Int64 扩展 + Bloom 旁路 POC 设计
meeting_id: meeting_014_poc_design
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 讨论记录 — Int64 扩展 + Bloom 旁路 POC 设计会议

## 参与者

| Agent | 角色 | 关注领域 |
|-------|------|----------|
| Architect | 架构师 | POC 架构、模块边界、与未来独立库的映射 |
| Algorithm | 算法工程师 | SIMD 正确性、搜索算法细节、性能建模 |
| Backend | 后端工程师 | 编译链路、数据结构、代码实现 |
| Fullstack | 全栈工程师 | 测试策略、集成验证、POC 执行流程 |
| Security | 安全专家 | 内存安全、边界条件、并发安全、intrinsic 正确性 |

---

## 讨论 1：POC 整体架构（议题 1）

### Architect 发言

```
我建议采用三文件结构，而非单文件：

poc_int64_avx2.c     — Path A 4 路 SIMD 二分（C1 验证）
poc_int64_b1.c        — Path B1 high20 dir + lo44 scan（C2 验证）
poc_bloom_bypass.c    — Bloom 旁路正确性 + 性能验证

理由：
1. 三个 POC 验证的维度不同：C1 是 SIMD 功能正确性，C2 是架构可行性，
   bloom_bypass 是特性正确性。分离为独立文件便于独立编译、独立验证。
2. 与项目现有惯例一致（src/ 下已有 8 个 poc_*.c 文件）。
3. POC 失败时可以精确定位是哪个维度不通过。
4. 保持"模式复制"（pattern replication）原则：
   每个 POC 文件结构相似但独立，为后续正式 libint64search 提供代码模板。
```

### Backend 发言

```
同意三文件结构。但我对编译依赖有顾虑。

现有的 poc_benchmark_v3.c 依赖 src/ 下的多个 .o 文件，导致编译命令复杂。
POC 应该是 gcc 单行编译的，这才符合"POC"的精神。

我建议每个 POC 文件完全自包含：
- 不 #include 任何 src/ 下的 .h 文件
- 内嵌必要的 xxhash 函数（或直接链接 src/xxhash/xxhash.o）
- 编译命令保持在 README.txt 中一行搞定
```

### Fullstack 发言

```
关于 bloom_bypass POC，我提议合入 poc_int64_avx2.c 作为一个独立测试段，
而不是单独文件。理由：
1. bloom_bypass 的正确性验证逻辑很简单（对比 bypass=0/1 结果），
   不需要独立文件。
2. meeting_013 的 ACT-13（性能对比）本身就是对 int32 现有库的验证，
   POC 阶段加一个 int64 bloom 快速验证就够了。

但 Architect 的观点也有道理——如果独立文件便于后续追溯。
我最终倾向：poc_bloom_bypass.c 独立，但保持精简。
```

### Architect 总结

```
采纳三文件结构。每个文件完全自包含，编译命令简单。
```

---

## 讨论 2：Path A int64 SIMD 核心算法（议题 3）

### Algorithm 发言

```
核心迁移映射：

int32 (8 路)                          int64 (4 路)
─────────────────────────────────────────────────────
_mm256_set1_epi32(target)     →    _mm256_set1_epi64x(target)
_mm256_loadu_si256(ptr)       →    _mm256_loadu_si256(ptr)  [不变]
_mm256_cmpgt_epi32(v, k)      →    _mm256_cmpgt_epi64(v, k)  [注意参数顺序]
_mm256_castsi256_ps(cmp)      →    _mm256_castsi256_pd(cmp)  [!!! 高危]
_mm256_movemask_ps(cmp_ps)    →    _mm256_movemask_pd(cmp_pd)

关键参数变更：
  mid & ~7  →  mid & ~3       // 对齐到 4 元素边界
  hi-lo>=8  →  hi-lo>=4        // 4 路并行
  8-popcnt  →  4-popcnt        // le_count 计算

⚠️ 高危注意（Security 在 meeting_012 已指出）：
_mm256_movemask_pd 从每个 64-bit lane 的最高位（bit 63）提取 sign bit，
而 _mm256_movemask_ps 从 bit 31 提取。如果遗漏这个变更，
使用 movemask_ps 去读取 epi64 比较结果，将从错误的 bit 位置读取，
导致 mask 值完全错误——要么假阴性，要么越界。

这是 C1 验证的最核心点。
```

### Security 发言

```
我补充三个边界测试要求：

1. _mm256_cmpgt_epi64 的 compare 语义必须明确：
   Intel intrinsic 文档：_mm256_cmpgt_epi64(a, b) 返回 a > b。
   如果写成 _mm256_cmpgt_epi64(key, vec)，语义是 key > vec，
   而我们想要 vec > key 来决定二分方向。请确认参数顺序！

   [Algorithm 插话]
   当前 search_avx2.c 写的是 _mm256_cmpgt_epi32(vec, key)，
   即 "vec 中的元素是否大于 key"。对 int64 应保持一致：
   _mm256_cmpgt_epi64(vec, key)。

2. 边界测试必须覆盖：
   - n=0（空数组，不应越界访问）
   - n=1~3（元素数 < 4，应跳过 SIMD 循环走标量回退，不能因 block=hi-4 下溢）
   - n=4（刚好一个 block，block=0，block+4=n=4，不应越界）
   - INT64_MIN / INT64_MAX 作为 target

3. movemask_pd 返回值位布局验证：
   movemask_pd 返回 4-bit mask，对应 lane 3..0。
   如果 lane 0 是 MSB，需要确认偏移计算正确。
```

### Backend 发言

```
编译命令：
gcc -O3 -std=c11 -mavx2 -o poc_int64_avx2 src/poc_int64_avx2.c -lm

不需要 -DINT32_SEARCH_AVX2 宏，因为 POC 文件自包含，
直接在代码中 #include <immintrin.h>。
```

---

## 讨论 3：Path B1 int64 架构（议题 4）—— 冲突讨论

### Algorithm 发言

```
B1 int64 的核心挑战是目录粒度的选择。

int32 B1 用 high16 dir（65536 个桶），因为 lo16 恰好是 2B uint16_t，
可以在一个 __m256i 寄存器中放 16 个 lo16 元素，用 _mm256_cmpeq_epi16 并行匹配。

int64 下，key 是 8B，如果取 high16 作为目录键（65536 桶），
每个桶内的元素用 int64_t 存储。在 AVX2 下只能并行比较 4 个 int64，
即一次 _mm256_cmpeq_epi64 最多处理 4 个候选。这意味着：
- 桶平均大小 ≥ 4 时，需要多次 SIMD 扫描
- 桶平均大小 ≥ 16 时，性能退化严重

替代方案：high20 dir（1M 桶, 4MB）。目录是 high16 的 16 倍，
但桶内元素更少。lo44 bucket scan 用 4 路 _mm256_cmpeq_epi64
并行扫描。

我的建模结果：
- high16 dir: 预期桶均 10 元素（10M 数据），SIMD 扫描 3 轮
- high20 dir: 预期桶均 10 元素（10M 数据），SIMD 扫描 3 轮
  差异不大。但 high20 dir 的 4MB 内存比 high16 dir 的 256KB 大 16 倍。

等等，我重新算：
- high16 dir (64K 桶), 10M/64K ≈ 152 元素/桶 — 这太大了！
- high20 dir (1M 桶), 10M/1M = 10 元素/桶 — 合理。
- high24 dir (16M 桶), 10M/16M ≈ 0.6 元素/桶 — 目录浪费内存。

所以 high20 是合理的选择。这与 meeting_012 D-092 的分析一致。
```

### Backend 发言（冲突点 1）

```
我的担忧是 B1 int64 的 POC 工作量。

high20 dir + lo44 bucket scan 需要实现：
1. 构建 high20 目录（分配 4MB malloc，遍历数据填充 dir[high20] 指针/偏移）
2. 扫描函数（4 路 _mm256_cmpeq_epi64 + 二次校验 vals[pos]==target）
3. 多分布 benchmark（uniform/skewed/zipf × 1M/5M/10M = 9 种组合）

这比 Path A POC 复杂一个数量级。Path A 只需要改写 50 行 SIMD 代码。

我提议：B1 POC 先做最简版本：
- 仅 uniform 分布
- 仅 1M 和 10M 两个规模
- 不做完整的 dir 构建，而是用简化的 hash 桶方案

如果 uniform 下 B1 都不如 Path A，那 B1 就没有继续的价值。
```

### Algorithm 反对 Backend 的简化方案

```
我不同意简化。原因：

1. D-092 明确要求"多分布 benchmark"，不能跳过。
   B1 在均匀分布下的性能不能代表倾斜分布下的表现。
   在 int32 下，B1 在倾斜分布上的退化已被 D-015 检测覆盖，
   int64 下也需要相同的验证。

2. 简化 hash 桶方案不能代表真实的高 20 位目录。
   meeting_012 的设计是 high20 dir + lo44 scan，
   我们必须在同样的架构下测试才能得到有意义的结论。

3. 但要承认 Backend 的工作量担忧是合理的。
   我建议折衷：POC 中 B1 实现一个"最小正确"版本，
   不做 COW、不做多线程、不做 bloom 集成。
   仅实现 dir 构建 + 查询 + benchmark。
```

### Architect 调解

```
同意 Algorithm 的折衷方案。

B1 POC 范围：
  ✅ high20 dir 构建（简单数组，不用 pool 方案）
  ✅ lo44 bucket 4 路 _mm256_cmpeq_epi64 扫描
  ✅ 二次校验 vals[pos]==target（假阳性防御）
  ✅ 3 分布 × 2 规模（uniform/skewed/zipf × 1M/10M）= 6 组
  ❌ COW、多线程、布隆集成（推迟到正式开发）
  ❌ 1M 桶目录的动态大小优化（先固定 4MB）
```

---

## 讨论 4：Bloom Bypass POC 范围（议题 5）

### Fullstack 发言

```
Bloom bypass POC 应该是一个轻量级的独立文件，验证 meeting_013 方案 C' 的核心逻辑：

1. 结构体：simulate int32_search_impl_t 的最小版本
   - _Atomic(int) bloom_bypass
   - _Atomic(void *) bloom（可为 NULL）

2. 搜索函数：最小化实现
   - 使用现有 int32 库的 search 逻辑
   - 在 bloom 预筛前增加 bypass 检查

3. 验证项（精简版）：
   a. bypass=0 时 bloom 预筛正常生效
   b. bypass=1 时跳过 bloom，搜索结果与 bypass=0 一致
   c. bypass 切换前后 find() 结果一致
   d. NULL bloom 句柄上 set_bloom_bypass 为安全 no-op
   e. 并发：2 线程同时切换 + 4 线程查询，无崩溃（不验证最终一致性）

4. 性能对比（可选）：
   - 1M 100% hit 场景，bypass=0 vs bypass=1 的 cycles/query 差异
```

### Security 发言

```
我在 meeting_013 中标记的 SEC-B1（rebuild bloom 交换顺序）在 POC 阶段
不要求修复——POC 不涉及 rebuild。但 POC 代码中不应引入新的安全问题。

对并发测试（验证项 e），要求：
- 使用 C11 atomic 而不是 volatile
- 查询线程使用 memory_order_relaxed 读取 bloom_bypass
- 不要求最终一致性，仅验证不崩溃
```

---

## 讨论 5：POC 验收标准与 Go/No-Go（议题 6）—— 关键决策

### Algorithm 发言

```
阻塞性条件（Blocking Gates）：

GATE-1 (C1): Path A SIMD 正确性
  验证 10000 次随机查询（n=100K）与 bsearch() 交叉验证零差异
  边界 n=0~15 全部通过
  → 不通过则 int64 二期不可行

GATE-2 (C1 性能): Path A AVX2 4 路 vs 标量二分加速比
  10M uniform 50% hit 下 AVX2 ≥ 1.2x 标量
  → 不通过则 int64 二期不可行（D-090 不可行条件）

GATE-3 (C2): Path B1 int64 vs Path A int64 加速比
  10M uniform 50% hit 下 B1 ≥ 1.5x Path A
  → 不通过则 B1 路径不纳入 Phase 1，接受"仅 Path A + 标量"的简化架构（D-092 降级方案）
  → 不影响 int64 二期整体可行性

GATE-4 (Bloom): bloom_bypass 正确性
  搜索结果 bypass=0/1 一致
  → 不通过则 bloom_bypass 特性推迟到 int64 Phase 2
```

### Architect 发言

```
POC 结束后需要产出的决策材料：

如果 GATE-1 + GATE-2 全部通过：
  → 输出《POC 通过报告》，含性能数据
  → 作为立项输入：启动 Align → Architect 流程
  → POC 代码作为 ARCHITECT 阶段的参考实现

如果 GATE-1 或 GATE-2 不通过：
  → 输出《POC 不通过报告》，记录具体失败原因和数据
  → 标记 int64 二期为 BLOCKED
  → 等待人工决策：是调整硬件前提（要求 AVX-512），还是放弃 int64
```

### Fullstack 发言

```
POC 性能数据的记录要求：

每个 benchmark 必须记录：
- 编译器和版本（gcc --version）
- CPU 型号和频率（lscpu / /proc/cpuinfo）
- 数据规模和分布
- 命中率
- 重复次数和中位数（至少 7 次重复取中位数，与现有 POC 惯例一致）
- 每条查询的 cycles 数（使用 __rdtsc()，排除预热）
```

---

## 讨论 6：POC 与立项的衔接（议题 6 延伸）

### Architect 发言

```
POC 结果与立项流程的关系：

POC 通过后，立项阶段（Align → Architect → Atomize → Approve）的输入包括：
1. POC 代码作为参考实现
2. POC 性能数据作为性能基线
3. meeting_012 D-090~D-096 决议作为需求基线
4. meeting_013 D-097~D-101 决议作为特性要求

立项阶段将产出：
- ALIGNMENT_int64_extension.md
- CONSENSUS_int64_extension.md
- DESIGN_int64_extension.md
- TASK_int64_extension.md

但这些都是立项阶段的工作，不属于本次 POC 会议的范围。
```

---

## 冲突汇总

| 编号 | 冲突点 | 涉及 Agent | 状态 |
|------|--------|-----------|------|
| 冲突 1 | B1 POC 工作量：Backend 倾向简化（1 分布 × 2 规模），Algorithm 坚持完整（3 分布 × 2 规模） | Backend vs Algorithm | ✅ 已解决：采纳折衷（3 分布 × 2 规模，不做 COW/多线程） |
| 冲突 2 | bloom_bypass POC 是否独立文件：Fullstack 倾向合入 int64 POC，Architect 倾向独立 | Fullstack vs Architect | ✅ 已解决：采纳 Architect 的三文件方案 |

---

## 增量日志

本次会议讨论 6 个议题，产生 2 个冲突（均已解决），将产出决议和行动项。
