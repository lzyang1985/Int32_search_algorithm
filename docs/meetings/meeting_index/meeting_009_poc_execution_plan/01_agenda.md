---
title: 会议议程 — POC 执行规划会
meeting_id: meeting_009_poc_execution_plan
status: Draft
created_at: 2026-05-30
updated_at: 2026-05-30
author: Human
parent_doc: docs/meetings/meeting_index/meeting_009_poc_execution_plan/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_008_b1_memory_pool/03_decisions.md
  - docs/meetings/meeting_index/meeting_008_b1_memory_pool/04_action_items.md
  - docs/meetings/meeting_index/meeting_007_poc_strategy/05_poc_execution_report.md
  - src/poc_benchmark_v3.c
tags: [poc, memory-pool, b1, execution-plan, phase2]
---

# 会议议程 — POC 执行规划会

## 1. 背景与现状

meeting_008 已冻结，D-060~D-070 全票通过。当前状态：

- **内存池不存在**：没有任何一行代码实现
- **3 项 P0 bug 未修复**：BUG-01（DIR 后向填充）、BUG-02（AVX2 搜索语义）、BUG-03（符号扩展）
- **现有 POC 代码**：仅 `src/poc_benchmark_v3.c`（339 行），包含 buggy 的 B1 实现
- **D-055 约束**：POC 代码必须单文件自包含

## 2. 议题一：POC 文件拆分策略

meeting_008 D-070 的行动链：

```
ACT-01~03 (修 Bug) → ACT-04 (B1 修复版 benchmark)
→ ACT-05 (内存池 POC) → ACT-06 (D-015 散点采集)
→ ACT-07 (合并到库源码)
```

**核心问题**：是创建 1 个大文件包含所有 POC 变体，还是拆成多个独立 POC 文件？

| 方案 | 文件数 | 优点 | 缺点 |
|------|:--:|------|------|
| A: 单文件演进 | 1 个（逐步改） | 简单的 diff | 无法同时跑三版本对比 |
| B: 三文件独立 | 3 个（fixed/pool/final） | 独立编译、可交叉对比 | 公共代码重复 |

## 3. 议题二：BUG 修复策略

### BUG-01：`high16_dir_build` 后向填充 off-by-one

[当前代码](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c#L73-L91)：

```c
for (size_t i = 0; i <= DIR_BUCKETS - 1; i++) dir[i] = -1;
dir[DIR_BUCKETS - 1] = (int32_t)n;  // dir[65536] = n

for (int32_t i = DIR_BUCKETS - 2; i >= 0; i--) {
    if (dir[i] == -1) dir[i] = dir[i + 1];
}
```

问题：当 `dir[i+1]` 本身也是 -1（未被前向填充覆盖）时，`dir[i] = dir[i+1]` 会把 -1 传播下去。实际上 `dir[65536]` 已初始化为 n，后向填充应该始终收敛到合法值……除非 `dir[65535] == -1` 且 `dir[65536]` 在某个路径下未被正确设为 n。需要精确分析触发条件。

### BUG-02：AVX2 搜索语义颠倒

[当前代码](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c#L133-L135)：

```c
__m256i cmp = _mm256_cmpgt_epi32(k, v);  // cmpgt(key, vec) — 判断 key > vec[i]
int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
int le_count = __builtin_popcount(mask ^ 0xFF);  // 取反 → "le" 语义
```

DEEP-05 证实这个版本的 hits=0。正确逻辑应该是 `cmpgt(vec, key)` 或重新设计比较语义。

### BUG-03：符号扩展漏洞

[当前代码](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c#L161)：

```c
uint16_t h = (uint16_t)((uint32_t)target >> 16);
```

这里 `B1 search` 中先转 `uint32_t` 再移位，是正确的。但 POC v3 中 AVX2 搜索路径用的是 `cmpgt(key,vec)` 语义。需确认 B1 路径本身的 h 提取是否安全。

## 4. 议题三：内存池 POC 实现细节

D-061 定义了内存布局：

```
偏移量(字节)      内容
0                dir[65537] (int32_t)   262148 B
262148           padding                28 B
262176           lo16[0..n-1] (uint16_t) 2n B
总大小 = 262176 + 2n B
```

**关键实现问题**：

1. Padding 策略：用 `memset(pool+262148, 0, 28)` 还是声明结构体 `struct { int32_t dir[65537]; char _pad[28]; uint16_t lo16[]; }`？
2. 查询入口：`lo16_base = (uint16_t*)(pool + 262176)` 还是通过宏 `B1_POOL_LO16(pool)`？
3. 构建流程：临时 dir → `dir_validate` → D-015 决策 → 分配 pool → `memcpy` dir → 填充 lo16
4. 对比 benchmark：在同一 main() 中跑三指针 B1 + 内存池 B1 + 标量二分

## 5. 各 Agent 讨论阵地

| Agent | 讨论焦点 |
|-------|----------|
| **Architect** | POC 文件拆分策略、与 D-055 约束的符合性、后续库源码合并路线 |
| **Algorithm** | Benchmark 方法设计：规模梯度、预热策略、对比维度、D-015 散点采集方案 |
| **Backend** | 内存池 C 实现细节：结构体设计、对齐处理、构建/查询/销毁流程 |
| **Security** | 3 项 bug 的精确修复策略、dir_validate 增强、ASan 验证方案 |
