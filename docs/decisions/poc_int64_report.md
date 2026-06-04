---
title: Int64 扩展 + Bloom 旁路 POC 结果汇总与 Go/No-Go 决策
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
reviewed_by: meeting_015
frozen_after: "crossover POC 完成 + D-114 修正全部完成"
author: Agent_Executor
source_meeting: meeting_014_poc_design
reviewed_by: meeting_015
source_docs:
  - "docs/meetings/meeting_index/meeting_012_int64_feasibility/"
  - "docs/meetings/meeting_index/meeting_013_bloom_toggle/"
  - "docs/meetings/meeting_index/meeting_014_poc_design/"
  - "docs/meetings/meeting_index/meeting_015_poc_result_review/"
trace_code:
  - "src/poc_int64_avx2.c"
  - "src/poc_int64_b1.c"
  - "src/poc_bloom_bypass.c"
tags: [poc, int64, avx2, bloom, decision, go-nogo]
---

# Int64 扩展 + Bloom 旁路 POC 结果汇总与 Go/No-Go 决策

> 执行日期：2026-06-02
> 执行环境：Linux Ubuntu 22.04, GCC 11.4.0, Intel Xeon Gold 6152 @ 2.10GHz (16核), 15GB RAM
> 源会议：[meeting_014_poc_design](../meetings/meeting_index/meeting_014_poc_design/04_action_items.md)

---

## 1. GATE 执行结果总览

| GATE | ACT | 验证项 | 结果 | 说明 |
|------|-----|--------|------|------|
| GATE-1 | ACT-14 | 正确性验证 (10000 交叉验证 + 边界 + 极值 + 重复 + bsearch) | ✅ **PASSED** | 0 failures, ASan/UBSan 零告警 |
| GATE-2 | ACT-14 | 性能基准 (10M uniform, 加速比 ≥ 1.2x) | ❌ **FAILED** | AVX2=2670 cy/q, Scalar=1560 cy/q, 加速比 0.58x |
| GATE-3 | ACT-15 | B1 ≥ 1.5x Path A (10M uniform) | ✅ **PASSED** | B1=318 cy/q, AVX2=2919 cy/q, 加速比 **9.17x** |
| GATE-4 | ACT-16 | Bloom 旁路 5 项验证 | ✅ **PASSED** | 0 failures, ASan/UBSan 零告警, 并发安全 |

### 1.1 GATE-2 新路径解释

meeting_014 D-106 决策树将 GATE-2 定义为「🔴 阻塞性，不通过 → int64 不可行」。POC 实际执行结果中 GATE-2 未通过（加速比 0.58x），但 GATE-3 超预期通过（9.17x ≫ 1.5x 门槛）。

经 meeting_015 五位专家评审一致（5/5，人工已签收），确认以下新 Go 路径：

> **GATE-2 的失败仅证伪「Path A AVX2 SIMD 二分」路线，不证伪「int64 扩展」本身。**
> GATE-3（9.17x）证明了 Path B1（high20 dir + lo44 scan）在 int64 下不仅可行且极为高效。
> 因此，Path A 降级为标量二分，Path B1 成为主线。int64 扩展整体判定为 **GO**。

此路径在 meeting_014 D-106 原始决策树中未被覆盖，已在 meeting_015 D-109 中正式确认。

---

## 2. 性能数据汇总

### 2.1 Path A — int64 AVX2 4路 SIMD 二分 (ACT-14)

| 指标 | 值 |
|------|-----|
| AVX2 cy/query | 2670.0 |
| Scalar cy/query | 1559.7 |
| 加速比 | **0.58x**（负加速） |

### 2.2 Path B1 — high20 dir + lo44 4路 SIMD 扫描 (ACT-15)

| 分布 | 规模 | max_bucket | B1 | Path A | Scalar | B1 vs AVX2 | B1 vs Scalar |
|------|------|-----------|------|--------|--------|------------|--------------|
| uniform | 1M | 8 | **144** | 1217 | 705 | 8.47x | 4.91x |
| uniform | **10M** | 26 | **318** | 2919 | 1688 | **9.17x** | **5.30x** |
| skewed 80/20 | 1M | 16 | **150** | 1117 | 664 | 7.45x | 4.43x |
| skewed 80/20 | 10M | 71 | **443** | 3162 | 1807 | 7.14x | 4.08x |
| ⚠️ zipf α=1.0 | 1M | 69,732 | 2036 | 1220 | 676 | 0.60x | 0.33x |
| ⚠️ zipf α=1.0 | 10M | 692,681 | 29,917 | 2745 | 1596 | 0.09x | 0.05x |

### 2.3 Bloom Bypass (ACT-16)

| 验证项 | 结果 |
|--------|------|
| V-BP-01: bypass=0 bloom 预筛 | ✅ 不存在 key 被 bloom 拦截 |
| V-BP-02: bypass=1 跳过 bloom | ✅ 走完整二分搜索 |
| V-BP-03: 切换一致性 | ✅ 100 key 切换前后结果一致 |
| V-BP-04: NULL bloom 安全 | ✅ 不崩溃 |
| V-BP-05: 并发安全 | ✅ 2 writer + 4 reader, 2 秒无崩溃 |

---

## 3. Go/No-Go 判定

### 3.1 Path A — int64 AVX2 4路 SIMD 二分: **NO-GO** ❌

**判定依据**：GATE-2 未通过（加速比 0.58x < 1.2x），AVX2 版本反而比标量慢 1.7x。

**失败根因**：
1. AVX2 256 位寄存器在 int64 下仅容纳 4 元素（对比 int32 的 8 元素），SIMD 摊还效率不足。
2. 二分查找每轮迭代依赖前次比较结果，SIMD 仅加速单轮内的 4 元素比较，无法减少 O(log n) 的迭代次数。
3. `_mm256_cmpgt_epi64` → `_mm256_castsi256_pd` → `_mm256_movemask_pd` → `__builtin_popcount` 指令链对标量比较而言开销过大。

**建议**：放弃 Path A int64 SIMD 路线，改用标量二分或 Path B1。

### 3.2 Path B1 — high20 dir + lo44 scan: **GO** ✅（条件性）

**判定依据**：GATE-3 通过（9.17x ≫ 1.5x），在 uniform 和 skewed 分布下表现卓越。

**附加条件**：
1. **必须实现回退机制**：参照现有 int32 `build_decision.c` 模式，增加 `B1_MAX_BUCKET_THRESHOLD_INT64`，当最大桶超过阈值时自动回退到标量二分。
   - **初始保守值 256（meeting_015 D-111）→ crossover POC 校准后推荐值 409** ✅
   - int64 B1 crossover POC 实测（2026-06-02，~1M 受控数据，100% hit）：
     | M | B1 cy/q | Scalar cy/q | B1/Scalar |
     |---|---------|-------------|-----------|
     | 8 | 159.7 | 773.6 | **0.21x** |
     | 64 | 431.8 | 767.0 | **0.56x** |
     | 256 | 611.0 | 794.0 | **0.77x** |
     | **512** | 762.6 | 795.2 | **0.96x**（交叉点） |
     | 1024 | 1131.2 | 794.7 | 1.42x |
     | 8192 | 4999.0 | 800.4 | 6.25x |
   - **交叉点 M=512**，取 0.8x 安全余量 → **推荐阈值 409**
   - 若直接复用 int32 阈值 2000，B1 在 M≈2000 时约 1550 cy/q，比标量（763 cy/q）慢 2.0x，不可接受
   - uniform 10M (max_bucket=26) 和 skewed 10M (max_bucket=71) 均 ≪ 409 ∴ 使用 B1 ✅
   - zipf 10M (max_bucket=692,681) ≫ 409 ∴ 回退到标量 ✅
2. **目录内存开销**：4 MB（`dir[1048577] × 4 bytes`），在服务器 15GB 内存下完全可接受。[DEBT] `int32_t dir` 类型在 n > 2^31 时溢出，若需支持超大数据集需改为 `int64_t`（8MB 目录）。
3. **Sign-flip 处理**：桶映射需使用 `((uint64_t)key ^ (1ULL<<63)) >> 44` 将 int64 有符号排序映射为无符号桶排序。**生产代码中构建和查询必须使用同一内联函数**，避免一致性 bug（meeting_015 Backend 发现 POC 中两处独立实现）。

**纳入 Phase 1 范围**：
- `search_int64_b1()` — 桶内 4 路 `_mm256_cmpeq_epi64` 并行扫描
- `build_dir_int64()` — high20 目录构建
- `build_decision_int64()` — 自动路径选择（B1 vs Scalar 回退）

### 3.3 Bloom Bypass: **GO** ✅

**判定依据**：GATE-4 全部通过，并发安全验证通过。

**纳入 Phase 1 范围**：
- `_Atomic(int) bloom_bypass` 运行时切换标志
- `poc_find()` 中 bypass 检查逻辑（方案 C'）
- 与现有 bloom 实现集成

### 3.4 安全评审摘要 (meeting_015 Security 评审)

meeting_015 代码安全专家独立评审了 POC 源码和报告。未发现 Critical 级安全漏洞，POC 代码安全等级 **可接受（Acceptable with Notes）**。

**主要发现**：

| 编号 | 问题 | 严重度 | 处置 |
|------|------|--------|------|
| SEC-POC-01 | `int32_t dir[1048577]` 在 n > 2^31 时溢出 | **Major** | Phase 1 DESIGN 标注容量限制，n > INT32_MAX 时拒绝或回退 |
| SEC-POC-03 | bloom_bypass 实现使用 `memory_order_acquire/release`，设计规范为 `relaxed` | Minor | 统一为 `relaxed`（meeting_013 D-098） |
| SEC-POC-04 | POC 中 `bloom` 指针非原子读取，POC 安全但模式不可直接复制到 COW 生产代码 | Minor | 生产代码必须用 `atomic_ptr_load`（对齐现有 `api.c` 模式） |
| SEC-POC-05 | POC bloom 使用自定义 MurmurHash3，非生产 XXH32/XXH64 | Minor | 集成时使用生产级 bloom 实现 |

**meeting_013 已知并发问题复查**：

| 编号 | 问题 | POC 阶段状态 |
|------|------|-------------|
| SEC-B1 | rebuild() bloom 交换顺序 → 假阴性窗口 | POC 无 rebuild，不适用。Phase 1 需修复 |
| SEC-B2 | impl->path 非原子读写 | POC 无多线程 path 切换，不适用 |
| SEC-B3 | B1 三指针非原子批量交换 | 已知设计权衡，Phase 1 DESIGN 中记录 |

**进入 Execute 阶段的安全前置条件**：
1. DESIGN 文档标注 `int32_t dir` 容量限制及回退策略
2. memory_order 统一为 `relaxed`
3. 生产代码确保 bloom 指针经 `atomic_ptr_load`
4. SEC-B1 bloom 交换顺序修复纳入 Phase 1 验收

---

## 4. 降级路径

对于 GATE-2 失败的 Path A：

| 方案 | 描述 | 优先级 |
|------|------|--------|
| A. 仅保留标量二分 | Path A 回退到纯标量 `search_int64_scalar()` | P0 |
| B. AVX-512 探索 | 如需 SIMD，可后续探索 AVX-512（8 元素/寄存器） | P2 |
| C. Eytzinger 布局 | 对 int64 数据探索缓存友好的 Eytzinger 排列 | P2（非本次范围） |

---

## 5. 下一步行动

### 5.1 完整 API 对标清单

以现有 `int32_search.h` API 为基线，int64 独立库（libint64search）完整 API 映射：

| int32 API | int64 对应 API | 优先级 | 说明 |
|-----------|---------------|--------|------|
| `int32_search_create()` | `int64_search_create()` | P0 | 模式复制，int64_t 数组 + int64_search_config_t |
| `int32_search_find()` | `int64_search_find()` | P0 | 返回 int，out_index 为 size_t* |
| `int32_search_destroy()` | `int64_search_destroy()` | P0 | 模式复制 |
| `int32_search_rebuild()` | `int64_search_rebuild()` | P0 | COW 重建，含 B1 路径 [DEBT] COW 原子交换方案待 DESIGN |
| `int32_search_version()` | `int64_search_version()` | P1 | 返回字符串 "libint64search v0.1.0" |
| `int32_search_find_range()` | `int64_search_find_range()` | P2 | Phase 2/3 范围查询 |
| `int32_search_set_bloom_bypass()` | `int64_search_set_bloom_bypass()` | P0 | 模式复制，_Atomic(int) + memory_order_relaxed |
| —（新增） | `int64_search_get_bloom_bypass()` | P1 | 对称 getter，调试/监控用 |

### 5.2 缺失设计决策补充（meeting_015 评审发现）

以下设计决策在原始报告中缺失，需在立项 Align 阶段确认：

| # | 决策项 | 说明 |
|---|--------|------|
| 1 | **dir 类型选择** | 当前 POC 用 `int32_t dir[1048577]`。若需支持 n > 2^31（约 21 亿条），需改为 `int64_t`（8MB 目录）。Phase 1 建议用 `int32_t` + `n > INT32_MAX` 断言 |
| 2 | **错误码命名空间** | `INT64_SEARCH_OK` vs 复用 `INT32_SEARCH_OK`。建议独立命名空间，避免调用方混淆 |
| 3 | **config_t 扩展** | int32 仅有 `use_bloom` + `reserved[7]`。int64 是否需要额外字段（如强制路径选择、阈值覆盖）？ |
| 4 | **库交付形式** | `libint64search.a`（独立静态库）vs 合并进 `libint32search.a`？独立库建议保持符号隔离 |
| 5 | **find_range 在 B1 路径下的策略** | B1 目录能否加速范围查询？还是统一回退到 vals[] 标量 lower_bound/upper_bound？ |

### 5.3 技术债务标注（DEBT）

| 编号 | 描述 | 优先级 | 预计解决阶段 |
|------|------|--------|-------------|
| DEBT-01 | B1 回退阈值已通过 int64 crossover POC 校准 → 推荐值 409 | P0 | ✅ 已完成 |
| DEBT-02 | `int32_t dir` 容量上限 2B 条，超出需改为 `int64_t` | P1 | Phase 2 评估 |
| DEBT-03 | int64 COW 原子交换方案待立项 DESIGN 设计 | P0 | Align/Architect |
| DEBT-04 | Sign-flip 公式需抽取为单一内联函数 | P0 | Phase 1 实现 |
| DEBT-05 | POC bloom（MurmurHash3）需替换为生产 XXH64 | P1 | Phase 1 集成 |

### 5.4 立项流程

1. **立项建议**（meeting_015 D-112 人工已签收 ✅）：
   - 进入 Align 阶段：编写 `ALIGNMENT_int64_b1.md`
   - 架构设计：`DESIGN_int64_b1.md`
   - 原子化拆分：`TASK_int64_b1.md`
   - 实施范围：Path B1 + 每桶回退 + Bloom Bypass 集成
   - **并行**：int64 B1 crossover POC（阈值校准）

2. **关键设计决策**（已在 meeting_015 D-110~D-113 决议）：
   - `B1_MAX_BUCKET_THRESHOLD_INT64` → 保守值 256，Phase 1 内校准
   - 独立 `int64_search_impl_t` 结构（不复用 `int32_search_impl_t`）
   - 独立库 `libint64search`（独立头文件 `int64_search.h`）
   - 共享平台抽象层（`platform_memory` / `platform_cpu` / `platform_thread`）

---

## 6. 关联信息

- **前置会议**：meeting_012_int64_feasibility, meeting_013_bloom_toggle, meeting_014_poc_design
- **POC 源代码**：
  - [src/poc_int64_avx2.c](../../src/poc_int64_avx2.c) — Path A 验证
  - [src/poc_int64_b1.c](../../src/poc_int64_b1.c) — Path B1 验证
  - [src/poc_bloom_bypass.c](../../src/poc_bloom_bypass.c) — Bloom 旁路验证
- **文档更新**：[README.txt](../../README.txt) — 新增 POC 编译专区

---

*本报告由 AI 助手生成于 POC 执行完成后，经 meeting_015 五位专家评审（5/5 全票通过）并人工签收 D-109 新 Go 路径。状态 Reviewing，待 int64 B1 crossover POC 完成后可 Frozen。*
