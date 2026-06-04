---
title: 决议记录 — Int64 扩展 + Bloom 旁路 POC 设计
meeting_id: meeting_014_poc_design
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
final_confirmation: "POC 三文件结构，覆盖 C1/C2/GATE-1~4 阻塞条件。POC 结果作为立项参考依据。"
---

# 决议记录 — Int64 扩展 + Bloom 旁路 POC 设计会议

## 📋 决议总览

| 决议编号 | 议题 | 结论 | 票数 |
|----------|------|------|------|
| D-102 | POC 文件结构 | 三文件独立自包含（poc_int64_avx2 / poc_int64_b1 / poc_bloom_bypass） | 5/5 |
| D-103 | Path A SIMD 算法规范 | 4 路 AVX2 二分，intrinsic 链完整映射，movemask 必须使用 _pd 变体 | 5/5 |
| D-104 | Path B1 int64 架构 | high20 dir (4MB) + lo44 bucket 4 路 _mm256_cmpeq_epi64 扫描 | 5/5 |
| D-105 | Bloom Bypass POC 范围 | 轻量独立文件，验证方案 C' 正确性 + 并发安全 | 5/5 |
| D-106 | POC 验收门控 (Go/No-Go) | 4 个 GATE，GATE-1/2 阻塞性，GATE-3 条件性，GATE-4 特性级 | 5/5 |
| D-107 | POC 执行与立项衔接 | POC 通过 → 立项；POC 不通过 → BLOCKED → 人工决策 | 5/5 |

---

## D-102：POC 文件结构

**结论：✅ 三文件独立自包含（5/5）**

```
src/
├── poc_int64_avx2.c       # Path A 4路SIMD二分验证（C1阻塞条件）
├── poc_int64_b1.c          # Path B1 high20 dir + lo44 scan验证（C2阻塞条件）
└── poc_bloom_bypass.c      # Bloom旁路正确性+并发验证
```

**设计原则**：
- 每个文件完全自包含：不 `#include` 任何 `src/` 下的 `.h` 文件
- 仅依赖标准库 + `<immintrin.h>` + `src/xxhash/`（仅 bloom POC）
- 编译命令在 README.txt 中记录，每份一行 `gcc` 命令
- 对标现有 POC 惯例（如 `poc_benchmark_v3.c`）

---

## D-103：Path A int64 SIMD 算法规范

**结论：✅ 完整映射方案（5/5）**

### Intrinsic 链映射

```c
/* ─── int32 (当前 8 路) ─── */
__m256i key = _mm256_set1_epi32(target);
__m256i vec = _mm256_loadu_si256((const __m256i *)(vals + block));
__m256i cmp = _mm256_cmpgt_epi32(vec, key);          // vec > key
int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
int le_count = 8 - __builtin_popcount((unsigned)mask);

/* ─── int64 (迁移 4 路) ─── */
__m256i key = _mm256_set1_epi64x(target);
__m256i vec = _mm256_loadu_si256((const __m256i *)(vals + block));
__m256i cmp = _mm256_cmpgt_epi64(vec, key);          // vec > key
int mask = _mm256_movemask_pd(_mm256_castsi256_pd(cmp));
int le_count = 4 - __builtin_popcount((unsigned)mask);
```

### 关键参数变更表

| 参数 | int32 (8 路) | int64 (4 路) |
|------|-------------|-------------|
| block 对齐 | `mid & ~(size_t)7` | `mid & ~(size_t)3` |
| while 条件 | `hi - lo >= 8` | `hi - lo >= 4` |
| 并行度 | 8 元素/寄存器 | 4 元素/寄存器 |
| le_count | `8 - popcount(mask)` | `4 - popcount(mask)` |
| movemask 函数 | `_mm256_movemask_ps` | `_mm256_movemask_pd` ⚠️ |
| 转换函数 | `_mm256_castsi256_ps` | `_mm256_castsi256_pd` ⚠️ |

### ⚠️ 高危标记

`_mm256_movemask_ps` → `_mm256_movemask_pd` 和 `_mm256_castsi256_ps` → `_mm256_castsi256_pd` 不可遗漏。遗漏将导致从错误的 bit 位置读取 sign bit（bit 31 而非 bit 63），产生完全错误的 mask，造成假阴性或越界。此为本 POC 最核心的验证点。

### 正确性验证要求

| 测试项 | 规模 | 验收标准 |
|--------|------|----------|
| 随机查询交叉验证 | n=100K, 10000 次查询 | 与 `bsearch()` 零差异 |
| n=0~3 边界 | n=0,1,2,3 | 不越界，走标量回退 |
| n=4 对齐边界 | n=4 | block=0, block+4=n=4 不越界 |
| n=5~15 混合 | n=5..15 | 部分 SIMD + 标量收尾正确 |
| INT64_MIN/MAX | target=INT64_MIN, INT64_MAX | 搜索正确 |
| 重复元素 | 多个重复 key | 返回第一个匹配位置 |

### 编译命令

```bash
gcc -O3 -std=c11 -mavx2 -o poc_int64_avx2 src/poc_int64_avx2.c -lm
```

---

## D-104：Path B1 int64 架构

**结论：✅ high20 dir + lo44 bucket 4 路扫描（5/5）**

### 数据结构

```c
/* high20 目录：2^20 = 1,048,576 个桶，每个桶一个 int32_t 偏移，总计 4MB */
int32_t dir[1 << 20];  /* dir[i] 指向 bucket i 在 sorted_vals 中的起始位置 */

/* 查询流程 */
size_t high20 = (size_t)(target >> 44);  /* 取高 20 位作为桶索引 */
size_t start  = dir[high20];
size_t end    = dir[high20 + 1];          /* dir 末尾有哨兵 */
/* lo44 bucket scan: 在 sorted_vals[start..end) 中查找 target */
```

### 桶内扫描算法（4 路 `_mm256_cmpeq_epi64`）

```c
while (i + 4 <= end) {
    __m256i key4 = _mm256_set1_epi64x(target);
    __m256i vec4 = _mm256_loadu_si256((const __m256i *)(vals + i));
    __m256i eq   = _mm256_cmpeq_epi64(key4, vec4);
    int mask = _mm256_movemask_pd(_mm256_castsi256_pd(eq));
    if (mask != 0) {
        int idx = i + __builtin_ctz(mask);
        if (vals[idx] == target) {  /* 二次校验 */
            *out_index = idx; return OK;
        }
    }
    i += 4;
}
/* 标量尾部 */
for (; i < end; i++) {
    if (vals[i] == target) { *out_index = i; return OK; }
}
```

### Benchmark 矩阵

| 分布 | 1M | 10M |
|------|-----|------|
| uniform | ✅ | ✅ |
| skewed (80/20) | ✅ | ✅ |
| zipf (α=1.0) | ✅ | ✅ |

共计 6 组，每组 7 次重复取中位数。

### 范围限制

POC 阶段不做：
- ❌ COW 多线程
- ❌ Bloom 集成
- ❌ 动态目录大小优化
- ❌ 内存池方案

### Go/No-Go 判定

`B1 cycles/query ≥ 1.5 × PathA cycles/query` → 纳入 Phase 1
`B1 cycles/query < 1.5 × PathA cycles/query` → 仅 Path A + 标量（降级方案）

---

## D-105：Bloom Bypass POC 范围

**结论：✅ 轻量级独立验证文件（5/5）**

### 验证项

| 编号 | 验证项 | 方法 | 验收 |
|------|--------|------|------|
| V-BP-01 | bypass=0 时 bloom 预筛生效 | 插入不存在的 key，验证 find() 返回 NOT_FOUND（被 bloom 拦截） | 拦截成功 |
| V-BP-02 | bypass=1 时跳过 bloom | 同一不存在的 key，bypass=1 后 find() 结果与 bypass=0 逻辑一致 | 结果一致 |
| V-BP-03 | bypass 切换前后一致性 | 存在 key：bypass 切换前后 find() 返回相同 index | 一致 |
| V-BP-04 | NULL bloom 安全 no-op | 无 bloom 句柄上 set_bloom_bypass(handle, 1) 不崩溃 | 无崩溃 |
| V-BP-05 | 并发安全 | 2 线程切换 bypass + 4 线程查询，不崩溃 | 无崩溃 |

### 数据结构

```c
/* POC 中模拟的最小 impl */
typedef struct {
    const int32_t *vals;
    size_t          n;
    void           *bloom;
    _Atomic(int)    bloom_bypass;   /* 0=normal, 1=bypass */
} poc_impl_t;
```

热路径变更：

```c
int _bypass = atomic_load(&impl->bloom_bypass, memory_order_relaxed);
if (!_bypass && bf != NULL && !bloom_query(bf, key)) {
    return NOT_FOUND;
}
```

### 依赖

- 需要链接 `src/xxhash/xxhash.o`（布隆内部使用 XXH32）
- 编译命令：

```bash
gcc -O3 -std=c11 -mavx2 -o poc_bloom_bypass src/poc_bloom_bypass.c src/xxhash/xxhash.o -lm
```

---

## D-106：POC 验收门控

**结论：✅ 4 级 GATE 体系（5/5）**

### GATE 定义

| Gate | 对应条件 | 性质 | 验证内容 | 判定 |
|------|----------|------|----------|------|
| **GATE-1** | C1 | 🔴 阻塞性 | Path A SIMD 交叉验证零差异 + 边界测试通过 | 不通过 → int64 不可行 |
| **GATE-2** | C5 | 🔴 阻塞性 | 10M uniform 下 AVX2 4路 vs 标量二分 ≥ 1.2x | 不通过 → int64 不可行 |
| **GATE-3** | C2 | 🟡 条件性 | 10M uniform 下 B1 vs Path A ≥ 1.5x | 不通过 → 仅保留 Path A |
| **GATE-4** | — | 🟢 特性级 | Bloom bypass 正确性验证全部通过 | 不通过 → bloom_bypass 推迟 |

### Go/No-Go 决策树

```
                ┌─────────────┐
                │ POC 执行完成 │
                └──────┬──────┘
                       │
              ┌────────▼────────┐
              │ GATE-1 通过？    │
              └────┬───────┬────┘
                   │ YES   │ NO
                   │       └──────────────┐
              ┌────▼────────┐             │
              │ GATE-2 通过？│             │
              └────┬────┬────┘             │
                   │YES │ NO              │
                   │    └─────────┐       │
              ┌────▼────┐         │       │
              │GATE-3?  │         │       │
              └──┬──┬───┘         │       │
            YES  │  │ NO          │       │
                 │  │             │       │
    ┌────────────▼──▼─────┐       │       │
    │ 🟢 全部通过          │       │       │
    │ 启动立项流程         │       │       │
    │ B1纳入Phase1         │       │       │
    └──────────────────────┘       │       │
           │                       │       │
    ┌──────▼──────┐                │       │
    │ 🟡 仅PathA   │                │       │
    │ 启动立项流程  │                │       │
    │ B1推迟Phase2 │                │       │
    └──────────────┘                │       │
           │                       │       │
    ┌──────▼───────────────────────▼───────┐
    │ 🔴 No-Go                            │
    │ int64 二期标记为 BLOCKED             │
    │ 输出《POC 不通过报告》，等待人工决策  │
    └──────────────────────────────────────┘
```

---

## D-107：POC 执行与立项衔接

**结论：✅ POC 通过后自动触发立项（5/5）**

### 衔接流程

```
POC 通过
  │
  ├── 产出《POC 通过报告》(docs/decisions/poc_int64_report.md)
  │     ├── GATE-1~4 全部通过状态
  │     ├── 性能数据摘要
  │     └── Go/No-Go 决策建议
  │
  └── 触发立项工作流 (Align 阶段)
        ├── 输入：POC 报告 + meeting_012/013 决议 + 总需求文档
        ├── 产出：ALIGNMENT_int64_extension.md
        │         CONSENSUS_int64_extension.md
        │         DESIGN_int64_extension.md
        │         TASK_int64_extension.md
        └── 目标：正式启动 Int64 二期工程 + Bloom Bypass 合并

POC 不通过
  │
  ├── 产出《POC 不通过报告》
  │     ├── 失败 GATE 及详细数据
  │     ├── 根因分析
  │     └── 建议（调整硬件前提 / 放弃 int64 / 仅 AVX-512）
  │
  └── 标记 int64 二期为 BLOCKED
        │
        └── 等待人工决策
```

### 执行顺序

```
Phase 1: Day 1-2
  poc_int64_avx2.c 开发 + 正确性验证 + 性能基线 (GATE-1, GATE-2)

Phase 2: Day 3-4
  poc_int64_b1.c 开发 + benchmark (GATE-3)

Phase 3: Day 5
  poc_bloom_bypass.c 开发 + 验证 (GATE-4)

Phase 4: Day 6
  汇总所有结果 → 产出《POC 报告》→ Go/No-Go 决策
```

---

## 📊 决议执行优先级

| 优先级 | 决议 | 行动 |
|--------|------|------|
| P0 | D-103 | 实现 poc_int64_avx2.c（Path A SIMD 验证 + 性能基线） |
| P0 | D-102 | 建立三文件 POC 骨架 + README.txt 编译命令 |
| P1 | D-104 | 实现 poc_int64_b1.c（Path B1 benchmark） |
| P1 | D-105 | 实现 poc_bloom_bypass.c（Bloom 旁路验证） |
| P1 | D-106 | POC 结果汇总 + Go/No-Go 判定 |
| P2 | D-107 | POC 通过后触发立项；不通过则输出 BLOCKED 报告 |
