---
title: 专家讨论记录 — B1 内存池方案讨论会
meeting_id: meeting_008_b1_memory_pool
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_008_b1_memory_pool/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_007_poc_strategy/05_poc_execution_report.md
  - docs/architecture/技术路线.md
  - docs/requirements/总需求文档.md
  - src/poc_benchmark_v3.c
tags: [b1, memory-pool, discussion, expert-opinions]
---

# 专家讨论记录 — B1 内存池方案讨论会

## 1. 用户提案重述

> "指针税不可避免，那就少交——直接内存池。int32 都是确定大小的，内存池也比较简单。高 16 位是索引比对中了，调到低 16 位一次指针税到底 16 位范围的开始处。实际上一开始就申请一块足够大的内存，高 16 位低 16 位都放在里面，到那个位置直接算就完了，都不用指针跳。"

---

## 2. 各专家独立分析

### 2.1 全栈开发工程师 — API/集成层面

**公开 API 影响：零变化。**
- `int32_search_t` 是不透明句柄（`void*`），调用方完全无感知
- `int32_search_config_t` 本次不需要变更（但建议 `reserved[0]` 未来支持强制路径选择）

**关键设计建议：放弃 `search_fn` 函数指针，改用 `switch(path)` 直接分发。**

理由：PATH_A 搜索签名是 `(vals, n, key, out_index)`，而 PATH_B1 需要额外传入 pool，两者无法统一到同一函数指针类型。`switch(path)` 方案在 `api.c:int32_search_find` 中仅需 3 行改动。

**`vals` 是否合并入池：不建议。**
- vals（40MB）是 PATH_A 和 PATH_B1 共享的，纳入 pool 会强制 PATH_A 也承担池化开销
- lo16（20MB）和 dir（262KB）适合合并，池大小 ~20MB

**测试兼容性：完全透明。** 所有测试通过公开 API 交互，无需修改。

**对齐注意**：dir 区 `65537 × 4 = 262148` 不是 32 的倍数（mod 32 = 4），lo16 需加 28 字节 padding 保证 32 字节对齐。

---

### 2.2 后端工程师 — 实现层面

**推荐内存池方案。** 在性能、安全性、COW 兼容性三方面均优于三指针方案。

**内存布局设计：**

```
偏移量(字节)      内容
─────────────────────────────────────────
0                dir[0..65536] (int32_t)   262148 B
262148           padding: 28 B
262176           lo16[0..n-1] (uint16_t)   2n B
总大小 = 262176 + 2n B
```

**dir 语义：保持 lo16 元素偏移（与当前 vals 索引等价，因为 lo16 和 vals 是 1:1 且保序）。**
`high16_dir_build` 不需要修改，dir 值就是 vals 索引 = lo16 偏移。

**构建流程（5 步）：**
1. `vals = build_sort_and_validate`（已有）
2. `dir_tmp = aligned_alloc(65537*4)` → `high16_dir_build(vals, n, dir_tmp)`
3. `dir_validate(dir_tmp)` + D-015 分布分析
4. 确定 PATH_B1 → `pool = aligned_alloc(262176 + 2n)` → memcpy dir → 填充 lo16
5. `aligned_free(dir_tmp)`

**销毁：从 3 次 free 降为 1 次（pool）+ 1 次（vals）。COW 从 4 字段快照降为 3 字段。**

---

### 2.3 架构师 — 架构层面

**价值判断：有价值，但收益主要在工程层面而非性能层面。**

核心理由：
- DEEP-05 已证明 AVX2 指令延迟是瓶颈，「指针税」消除最多带来 5-15% 增量
- 真正的价值在于：架构简化（3 malloc→1 malloc）、COW 简化（seq_lock→无锁单指针）、内存管理简化

**推荐路线 B：vals 索引式 dir + 平行 lo16 合并到单 pool。**
- 拒绝路线 A（lo16 嵌入反向索引）：过度设计，破坏 SIMD 连续性
- 拒绝 vals 入池：无收益，增加复杂性

**关键技术风险：**
- **B1 POC bug 修复必须先于内存池设计**。没有可信基线之前设计优化是盲目的。
- D-015 决策阈值必须重新校准（POC v3 数据被 AVX2 搜索 bug 污染）
- COW 从 `seq_lock` 迁移到无锁单指针是重要的架构红利

**最大担忧**：lo16 扫描命中后回验 vals 需要从 vals 索引映射回 lo16 偏移——在路线 B 下两者是 1:1 对应，自然成立。但如果未来有人试图优化 lo16 顺序（如按桶重排），这个 1:1 对应会被破坏。

---

### 2.4 算法工程师 — 性能层面

**预期性能：综合约 3.5x-3.8x vs 标量二分。**
- 基础 B1（bug 修复后）：约 3.2x-3.6x
- 内存池增量：额外 5-8%（冷路径 prefetch 改善 + TLB 收益）

**「指针税」的真相**：
- 热稳态下指针解引用仅 ~10 cycles，约占总延迟的 3-5%
- 真正的池化收益来自：冷路径 TLB miss 减少、硬件预取器跨越 dir→lo16 的连续性
- **vals 回验占延迟的 60-70%**，这是绝对瓶颈，内存池对此无能为力

**lo16 SIMD 扫描（~40-60 cy）**：
- AVX2 `_mm256_cmpeq_epi16` 每指令 16 个元素，均匀分布下仅需 10 次迭代
- 优化建议：小桶（<16）降级标量；大桶做循环展开
- **Not critical path**（回验才是）

**优先级建议**：
1. P0：修复 B1 bug → 获取可靠基线
2. P1：实施内存池 → 验证 5-8% 收益
3. P2：投机预取 `vals[pos]` → 预期额外 10-15%
4. P3：lo16 扫描调优（展开、混合策略）

---

### 2.5 代码安全专家 — 安全层面

**单内存池在安全性上优于三指针方案。**

**关键发现：符号扩展漏洞（C-001）**

```c
uint32_t h = key >> 16;  // ❌ key 是 int32_t，负数时算术右移！
INT32_MIN >> 16 = 0xFFFF8000  // 符号扩展 → 越界访问 dir[4294934528]
```

**修正**：必须先转 `(uint32_t)key` 再移位，然后检查 `h < 65536`。

**必须实现的 4 项 dir_validate 检查（生产构建保留）**：
1. `dir[65536] == N`（哨兵）
2. `dir[h] <= dir[h+1]`（单调性）
3. `0 <= dir[h] <= N`（范围约束）
4. `dir[0] == 0`（起始约束）

**内存泄漏防护**：从 3 次 free 需正确回滚 → 1 次 free 全有或全无，泄漏风险面大幅缩小。

**ASan 友善性**：单块大分配意味着 ASan 红区更少，越界检测更精确。但 dir 紧邻 lo16 意味着 dir 的越界写入可能污染 lo16——这恰反过来说明 `dir_validate` 不能省。

---

## 3. 交叉讨论：意见收敛与分歧

### 3.1 收敛点（全票通过）

| 议题 | 共识 |
|------|------|
| 是否做内存池？ | ✅ **值得投入**（5/5） |
| vals 是否入池？ | ❌ **不入池**（5/5） |
| 是否需要 28B padding？ | ✅ **必须**（Backend + Fullstack + Algorithm） |
| 是否先修 B1 bug？ | ✅ **必须先修**（Architect + Algorithm + Security） |
| dir_validate 生产构建保留？ | ✅ **必须**（Security + Backend + Architect） |
| 符号扩展漏洞？ | ⚠️ **C-001 严重漏洞**，所有方案均受影响（Security） |

### 3.2 轻微分歧（需决议）

| 议题 | Architect | Backend | Algorithm | 建议决议 |
|------|-----------|---------|-----------|----------|
| dir 语义 | vals 索引（与 lo16 1:1 对应） | lo16 元素偏移（与 vals 索引等价） | 无所谓，1:1 即可 | **统一：lo16 元素偏移（= vals 索引，因为平行数组）** |
| 回验优化 | 不急于处理 | 不在本次范围 | P2 做投机预取 | **延至 P2，先拿可信数据** |
| search_fn 派发 | 保留 + 加分支 | switch(path) | — | **switch(path)**（Fullstack 详细论证） |
| D-015 阈值 | 必须重新校准 | 必须重新校准 | 必须重新校准 | **POC 阶段采集散点，不固定阈值** |

### 3.3 架构师 vs 用户的微妙差异

用户提案隐含 dir 直接指向 lo16 区域内的偏移（「到那个位置直接算就完了都不用指针跳」）。Architect 推荐的路线 B 保留了 dir → vals 索引 → lo16 的 1:1 对应关系，只是把 dir 和 lo16 放入同一 pool。实际效果等价——dir 的数值同时是 vals 索引和 lo16 偏移。之所以保留 vals 索引语义是为了与现有 POC 代码兼容，降低移植风险。

---

## 4. 讨论总结

本次讨论达成高度共识。**单内存池方案是 B1 路径的正确工程方向**，主要收益在架构简化（COW 无锁化、内存管理安全化）而非绝对性能提升（预期 +5-8%）。

**先决条件**（必须在内存池 POC 之前完成）：
1. 修复 `high16_dir_build` 后向填充 off-by-one bug
2. 修复 POC v3 中 `cmpgt(key,vec)` 语义错误 → 改用正确 AVX2 搜索逻辑
3. 修复符号扩展漏洞：`(uint32_t)key >> 16` + 范围检查
4. 用修复后的 B1 重新 benchmark（1.5M / 5M / 10M, uniform + skewed）

**内存池方案设计决策**：
1. pool = dir[65537](int32_t) + 28B padding + lo16[n](uint16_t)，总大小 = 262176 + 2n B
2. vals 保持独立分配（Path A/B1 共享）
3. `internal.h` 结构体：`vals` + `pool` + `n` + `path`
4. `api.c` 中用 `switch(path)` 替代 `search_fn` 函数指针
5. dir 语义保持 lo16 元素偏移（与 vals 索引 1:1 等价）
6. D-015 阈值暂不固定，POC 阶段采集散点确定 crossover

**COW 收益**：从 `{vals, lo16, dir, n}` 4 字段（32B，不可原子）→ `{pool, n}` 2 字段（16B，可 DCAS），实现真正的无锁并发读取。

---

> 以上讨论记录待人工审阅后，生成决议文档（03_decisions.md）。
