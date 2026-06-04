---
title: 议程 — Int64 扩展 + Bloom 旁路 POC 设计
meeting_id: meeting_014_poc_design
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 议程 — Int64 扩展 + Bloom 旁路 POC 设计会议

## 会议背景

meeting_012（Int64 扩展可行性）判定 Int64 扩展为"有条件可行"，6 项核心条件中 C1（Path A SIMD 功能验证）和 C2（Path B1 独立 POC benchmark）为阻塞性条件。meeting_013（Bloom 开关）通过了方案 C'（`_Atomic(int) bloom_bypass` + setter），ACT-07 为 P0 实现项。两场会议共计 13 项行动项尚未启动。

本次会议目标：设计一套集中验证核心阻塞条件的 POC 测试方案。POC 结果将作为是否正式启动 Int64 二期工程立项的参考依据。

---

## 议题 1：POC 整体架构设计

**输入**：
- meeting_012 ACT-01~ACT-06
- meeting_013 ACT-07（bloom_bypass P0 实现）
- 项目现有 POC 惯例：单文件自包含（`src/poc_*.c`），可 `gcc` 直接编译

**需讨论**：
1. POC 采用单文件还是多文件结构？
2. 是否复用现有 `internal.h` / `search_avx2.c` 代码，还是独立重写？
3. 编译依赖（xxHash、现有源文件）如何处理？

---

## 议题 2：POC 覆盖范围与优先级排序

**前提**：POC 首要目标是验证 C1（Path A SIMD 正确性）和 C2（Path B1 可行性）。Bloom bypass 为 P0 实现项，可纳入 POC 验证。

**需讨论**：
1. 四份 POC 代码的边界划分：
   - `poc_int64_avx2.c` — Path A 4 路 SIMD 二分
   - `poc_int64_b1.c` — Path B1 high20 dir + lo44 scan
   - `poc_bloom_bypass.c` — Bloom 旁路正确性 + 性能
   - 是否需要第四份 `poc_int64_bench.c` 做 10M 全量基线？
2. 各 POC 的优先级和执行顺序
3. POC 是否需要覆盖 AVX-512 预留（ACT-05）？还是一律推迟到 Phase 2/3？

---

## 议题 3：Path A int64 SIMD 验证方案设计

**核心问题**：确认 `_mm256_cmpgt_epi64` + `_mm256_movemask_pd` intrinsic 链在 GCC ≥ 8.0 上行为正确。

**需讨论**：
1. SIMD 搜索核心循环的精确写法（对标现有 `search_avx2_find()` 的 int32 8 路版本迁移到 int64 4 路）
2. 关键参数变更：
   - block 对齐 `mid & ~7` → `mid & ~3`
   - while 条件 `hi - lo >= 8` → `hi - lo >= 4`
   - le_count `8 - popcount(mask)` → `4 - popcount(mask)`
   - `_mm256_movemask_ps` → `_mm256_movemask_pd`（高危注意）
3. 正确性验证方案：
   - 1000+ 随机 key vs `bsearch()` 交叉验证
   - n=0~15 边界测试（空数组、单元素、对齐边界）
   - 重复元素场景
4. 编译命令：`gcc -O3 -std=c11 -mavx2 -o poc_int64_avx2 poc_int64_avx2.c`

---

## 议题 4：Path B1 int64 架构验证方案设计

**核心问题**：int64 B1 的 high20 dir + lo44 bucket scan 是否具备实用价值。

**需讨论**：
1. B1 int64 数据结构：high20 dir（2^20=1M entries, 4MB）+ lo44 bucket scan（4 路 `_mm256_cmpeq_epi64` 并行）
2. 与 int32 B1 的关键差异：
   | 不变量 | int32 | int64 |
   |--------|-------|-------|
   | 目录粒度 | high16 dir (65536, 256KB) | high20 dir (1M, 4MB) |
   | 桶内元素类型 | uint16_t (2B) | int64_t (8B) |
   | 并行度 | 16 路 `_mm256_cmpeq_epi16` | 4 路 `_mm256_cmpeq_epi64` |
3. Benchmark 方案：1M / 5M / 10M，uniform / skewed / zipf
4. Go/No-Go 判定标准：B1 int64 ≥ 1.5x vs Path A int64

---

## 议题 5：Bloom Bypass POC 验证方案

**核心问题**：验证 meeting_013 方案 C' 的正确性和性能收益。

**需讨论**：
1. POC 是验证现有 int32 库的 bloom_bypass（ACT-07），还是同步验证 int64 的 bloom_bypass？
2. 正确性验证：
   - bloom_bypass=0 与 bloom_bypass=1 搜索结果一致
   - 无 bloom 句柄上调用为安全 no-op
   - 并发切换 + 查询不崩溃
3. 性能验证：Path B1 1M 100% hit 场景 bypass vs normal 延迟对比

---

## 议题 6：POC 验收标准与 Go/No-Go 决策

**需讨论**：
1. 阻塞性条件（不满足则 int64 二期不可行）：
   - C1：Path A SIMD 交叉验证零差异
   - C2：B1 int64 vs Path A 加速比 ≥ 1.5x（条件性：若 B1 不达标，接受"仅 Path A + 标量"的简化架构）
   - 性能底线：AVX2 4 路 vs 标量二分加速比 ≥ 1.2x（D-090 不可行条件）
2. 质量门控条件：
   - C5：10M uniform 下 AVX2 4 路实测 cycles/query
3. POC 结果如何转化为立项输入：
   - POC 通过 → 启动正式立项流程（Align → Architect → Atomize → Approve）
   - POC 不通过 → 记录原因 → 标记为 BLOCKED → 等待人工决策

---

## 议题 7：POC 与现有代码的关系

**需讨论**：
1. POC 代码落位：`src/poc_int64_*.c`（与现有 POC 文件同级）
2. POC 是否需要引入新的编译目标到 Makefile？还是仅提供 README.txt 命令？
3. POC 期间是否修改现有 int32 库代码？（否，仅新增独立文件）

---

## 参考文档

| 文档 | 来源 |
|------|------|
| meeting_012 决议 (D-090~D-096) | `meeting_012_int64_feasibility/03_decisions.md` |
| meeting_012 行动项 (ACT-01~ACT-06) | `meeting_012_int64_feasibility/04_action_items.md` |
| meeting_013 决议 (D-097~D-101) | `meeting_013_bloom_toggle/03_decisions.md` |
| meeting_013 行动项 (ACT-07~ACT-13) | `meeting_013_bloom_toggle/04_action_items.md` |
| 现有 search_avx2.c | `src/search_avx2.c` |
| 技术路线文档 | `docs/architecture/技术路线.md` |
