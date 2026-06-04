---
title: 决议文档 — B1 内存池方案讨论会
meeting_id: meeting_008_b1_memory_pool
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_008_b1_memory_pool/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_008_b1_memory_pool/02_discussion.md
  - docs/meetings/meeting_index/meeting_007_poc_strategy/05_poc_execution_report.md
tags: [b1, memory-pool, decision, phase2]
---

# 决议文档 — B1 内存池方案讨论会

> 5/5 专家参与。以下决议草案待人工确认 Frozen。

---

## D-060：采纳单内存池方案作为 B1 路径的正式架构

**投票**：5/5 通过（Architect, Algorithm, Backend, Fullstack, Security 全票）

**内容**：
B1 路径的 dir（高 16 位目录）和 lo16（低 16 位数组）从两块独立 `malloc` 合并为一块连续内存池 `pool`。vals（原始排序数组）保持独立分配。

**理由**：
1. COW 简化：`{vals, lo16, dir, n}` 4 字段 32B（不可原子）→ `{pool, n}` 2 字段 16B（可 DCAS），实现无锁并发读取
2. 内存管理：2 次独立分配/释放 → 1 次，消除部分分配失败的回滚复杂度
3. 缓存局部性：dir 和 lo16 物理连续 → 硬件预取器可跨越 dir→lo16
4. 性能增量：预期 +5-8%（冷路径 TLB/prefetch 改善）
5. 安全：减少内存泄漏面，ASan 越界检测更精确

---

## D-061：内存池官方布局

**投票**：5/5 通过

**内存布局**：

```
偏移量(字节)      内容                          大小
─────────────────────────────────────────────────────
0                dir[65537] (int32_t)           262148 B
262148           padding (memset to 0)          28 B
262176           lo16[0..n-1] (uint16_t)        2n B
─────────────────────────────────────────────────────
总大小 = 262176 + 2n B
```

**理由**：
- dir 区 262148 mod 32 = 4，lo16 起始偏移 262148 不对齐 32B
- 28B padding 使 lo16 起始于 262176（262176 mod 32 = 0），保证 `_mm256_loadu_si256` 不跨 cache line 边界
- dir 末条 `dir[65536]` 在 262144~262147 位置，位于独立 32B 块，不受 lo16 影响

---

## D-062：dir 语义：lo16 元素偏移（与 vals 索引等价）

**投票**：5/5 通过

**内容**：
`dir[h]` 存储的是 **lo16 数组中桶 h 的第一个元素在 lo16 中的偏移**。由于 lo16 是 vals 的平行数组（lo16[i] 对应 vals[i]），这与当前 POC v3 中 dir 的 vals 索引语义**完全等价**，不需要值变换。

**查询定位**：
```c
const uint16_t *lo16_base = (const uint16_t *)((const char *)pool + 262176);
int32_t start = dir[h];    // lo16 中的起始偏移
int32_t end   = dir[h+1];  // lo16 中的终止偏移
// 扫描 lo16_base[start .. end-1]
```

**弃用路线 A**（lo16 嵌入反向索引）：破坏 SIMD 连续性，内存膨胀 4x，5/5 反对。

---

## D-063：vals 不入池，保持独立分配

**投票**：5/5 通过

**理由**：
- vals 是 PATH_A 和 PATH_B1 的共享资源
- PATH_A 不需要 pool，强制入池浪费 256KB+
- lo16 和 vals 的访问模式完全不同（连续扫描 vs 随机点查），放入同一区域无 prefetch 协同效应
- COW 中 vals 和 pool 各自独立，可以独立管理生命周期

---

## D-064：api.c 中采用 switch(path) 替代 search_fn 函数指针

**投票**：5/5 通过

**理由**：
- PATH_B1 搜索函数需要额外传入 `pool` 参数，无法统一到 `search_fn(vals, n, key, out_index)` 签名
- 当前 `search_fn` 只被 `int32_search_find` 一处调用，改为 `switch(path)` 改动仅 3 行
- 不会破坏 Phase 1 的 PATH_A 代码路径

**实现示意**：
```c
// int32_search_find 中
switch (impl->path) {
case PATH_A:
    return (impl->avx2_capable && impl->n > INT32_SEARCH_AVX2_MIN_N)
        ? search_avx2_find(impl->vals, impl->n, key, out_index)
        : search_scalar_find(impl->vals, impl->n, key, out_index);
case PATH_B1:
    return search_b1_pool_find(impl->pool, impl->vals, impl->n, key, out_index);
}
```

---

## D-065：P0 先决条件 — 内存池 POC 前必须修复 3 项 bug

**投票**：5/5 通过

| 编号 | Bug | 严重度 | 位置 | 描述 |
|:--:|------|:--:|------|------|
| BUG-01 | `high16_dir_build` 后向填充 off-by-one | **HIGH** | `poc_benchmark_v3.c:L73-91` | 空桶后向填充时 `dir[i] = dir[i+1]` 在边界条件 `dir[i]==-1` 且 `dir[i+1]==n` 时不一致 |
| BUG-02 | AVX2 搜索逻辑语义错误 | **CRITICAL** | `poc_benchmark_v3.c` 中的 AVX2(A) 路径 | `cmpgt(key,vec)` + `le_count = popcount(mask ^ 0xFF)` 语义颠倒，hits=0，污染整个 POC v3 数据 |
| BUG-03 | 符号扩展漏洞 (C-001) | **CRITICAL** | 所有 B1 路径 | `uint32_t h = key >> 16` 对负数 key 触发算术右移（符号扩展），导致 dir 灾难性越界。必须改为 `h = (uint32_t)key >> 16` 然后检查 `h < 65536` |

**流程**：以上 3 项 bug 必须先于内存池 POC 修复，并用修复后的 B1 重新 benchmark 获取可信基线。

---

## D-066：dir_validate 增强并生产构建保留

**投票**：5/5 通过

**内容**：`dir_validate` 增加为 4 项检查（原 2 项），生产构建强制运行：

| 检查 | 条件 | 失败影响 |
|------|------|----------|
| 1. 哨兵 | `dir[65536] == n` | 返回 NULL（dir 构建系统性错误） |
| 2. 单调性 | `dir[h] <= dir[h+1]` for all h | 返回 NULL |
| 3. 范围 | `0 <= dir[h] <= n` for all h | 返回 NULL |
| 4. 起始 | `dir[0] == 0` | 返回 NULL |

**失败处理**：`int32_search_create` 返回 NULL（优雅降级为调用方自行处理），不 abort。

---

## D-067：内部结构体重设计

**投票**：5/5 通过

**新 `int32_search_impl_t`**：

```c
typedef struct {
    int             path;
    size_t          n;
    int32_t        *vals;         /* 排序副本，Path A/B1 共享 */
    uint8_t        *pool;         /* B1 内存池（dir + lo16），Path A 时为 NULL */
    size_t          pool_size;    /* pool 总字节数，用于 destroy */
    uint8_t         avx2_capable; /* CPUID 检测结果 */
} int32_search_impl_t;
```

**移除**：`search_fn` 函数指针（由 D-064 switch(path) 替代）。

---

## D-068：D-015 决策阈值暂不固定

**投票**：5/5 通过

**内容**：
POC v3 的 `max_sz ≤ 150 → PATH_B1` 阈值基于 buggy 数据，无效。内存池 POC 阶段采集 `max_bucket → cy/q` 散点（横轴桶大小，纵轴周期数），观察真实 crossover 后再固定阈值。

**规模梯度**：100K / 500K / 1.5M / 5M / 10M
**分布**：uniform + skewed + zipf（至少前两种）
**命中率**：0% / 50% / 100%

---

## D-069：COW 架构红利确认

**投票**：5/5 通过

**内容**：
内存池方案使 B1 COW 快照从 4 字段（32B，不可原子交换）退化为 2 字段（16B，x86-64 `lock cmpxchg16b` 可原子）。实现真正的无锁并发读取（零锁 acquire load），Phase 1.5 的 COW 实现将显著简化。

---

## D-070：下一阶段行动计划

| 编号 | 行动 | 优先级 | 依赖 |
|:--:|------|:--:|------|
| ACT-01 | 修复 BUG-01（DIR 后向填充） | P0 | — |
| ACT-02 | 修复 BUG-02（AVX2 搜索语义） | P0 | — |
| ACT-03 | 修复 BUG-03（符号扩展漏洞） | P0 | — |
| ACT-04 | 修复后 B1 POC benchmark（单文件 `src/poc_b1_fixed.c`） | P0 | ACT-01~03 |
| ACT-05 | 内存池 POC（单文件 `src/poc_b1_pool.c`） | P1 | ACT-04 |
| ACT-06 | D-015 散点数据采集 | P1 | ACT-05 |
| ACT-07 | 合并到库源码（`build_dir.c` / `build_b1.c` / `search_b1.c`） | P2 | ACT-05 + D-015 定阈 |

---

> 以上决议草案待人工审阅确认 Frozen。确认后将生成 04_action_items.md 详细行动清单。
