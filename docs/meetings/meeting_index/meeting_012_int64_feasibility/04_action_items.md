---
title: 行动项 — Int64 扩展可行性研讨
meeting_id: meeting_012_int64_feasibility
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 行动项 — Int64 扩展可行性研讨会议

## 📋 行动项总览

| 编号 | 行动项 | 优先级 | 负责人 | 依赖 | 状态 |
|------|--------|--------|--------|------|------|
| ACT-01 | Path A int64 POC 实现与 intrinsic 验证 | P0 | 待定 | 无 | Pending |
| ACT-02 | 独立库架构决议正式文档 | P0 | 待定 | 无 | Pending |
| ACT-03 | Path B1 int64 POC benchmark | P1 | 待定 | ACT-01 | Pending |
| ACT-04 | Path A MVP 10M 性能基线测试 | P1 | 待定 | ACT-01 | Pending |
| ACT-05 | AVX-512 编译宏预留 | P2 | 待定 | ACT-01 | Pending |
| ACT-06 | Bloom XXH32→XXH64 评估 | P3 | 待定 | 无 | Pending |

---

## 行动项详情

### ACT-01：Path A int64 POC 实现与 intrinsic 验证 [P0]

**目标**：验证 int64 Path A 在 AVX2 下的功能正确性和性能

**交付物**：
1. `src/poc_int64_avx2.c` — 单文件自包含 POC
   - `_mm256_set1_epi64x` + `_mm256_cmpgt_epi64` + `_mm256_movemask_pd` 实现
   - block 对齐 4 元素，while 条件 `>= 4`
   - 标量尾部回退
2. 1000+ 随机查询结果与 `bsearch()` 交叉验证零差异
3. n=0~15 边界值测试通过
4. 10M uniform 50% hit benchmark 实测 cycles/query

**阻塞条件 C1**：确认 intrinsic 链在 GCC ≥ 8.0 上行为正确

---

### ACT-02：独立库架构决议正式文档 [P0]

**目标**：将 D-093（独立库 libint64search）固化为正式架构决定

**交付物**：
1. 更新技术路线文档 `docs/architecture/技术路线.md`
   - 新增 §9: "二期工程 Int64 扩展路线（条件性可行）"
   - 记录 D-090~D-096 决议
2. 创建 `docs/rfc/rfc_002_int64_extension.md`
   - 记录二期工程触发原因
   - D-093 API 形态决议
   - C1-C6 条件清单

---

### ACT-03：Path B1 int64 POC benchmark [P1]

**目标**：验证 int64 B1（high20 dir + int64 scan 变体）的实用价值

**交付物**：
1. 实现 high20 dir (4MB/8MB) + int64 bucket scan (4路 `_mm256_cmpeq_epi64`)
2. 多分布 benchmark：1M / 5M / 10M，uniform / skewed / zipf
3. 与 Path A int64 和标量二分对比
4. Go/No-Go 判定：B1 int64 变体是否提供足够性能增量（≥ 1.5x vs Path A）

**阻塞条件 C2**：不可假设 int32 B1 经验可迁移

---

### ACT-04：Path A MVP 10M 性能基线测试 [P1]

**目标**：确立 int64 Path A 的基准性能数据

**交付物**：
1. 10M uniform 50% hit benchmark 结果（cycles/query）
2. int64 标量二分 baseline 对比
3. 加速比报告（AVX2 4路 / 标量 1路）
4. 与当前 int32 Path A 数据对比

**阻塞条件 C5**：10M 下加速比作为 Phase 1 质量门控

---

### ACT-05：AVX-512 编译宏预留 [P2]

**目标**：在 int64 源码中预留 AVX-512 编译路径

**交付物**：
1. POC 代码中添加 `#ifdef INT64_SEARCH_AVX512` 编译分支
2. 框架级 `_mm512_cmpgt_epi64_mask` 测试可用性
3. 不实现完整 AVX-512 搜索算法（留待 Phase 2/3）

**阻塞条件 C4**：设计预留，不阻塞 Phase 1

---

### ACT-06：Bloom XXH32→XXH64 评估 [P3]

**目标**：确认 int64 key 下 XXH64 的假阳性率和性能影响

**交付物**：
1. xxHash 库已内嵌于 `src/xxhash/`，确认 XXH64 可用
2. bloom_insert 从 `XXH32(&key, 4, ...)` 升级为 `XXH64(&key, 8, ...)`
3. 1M 假阳性率 benchmark（目标 < 1%）

---

## 📅 建议执行顺序

```
Week 1:
  Day 1-2:  ACT-01 Path A POC 实现 + 交叉验证
  Day 2:    ACT-05 AVX-512 预留（与 ACT-01 同步）
  Day 3-4:  ACT-04 10M 性能基线测试
  Day 5:    ACT-02 架构文档（基于 ACT-01/04 结果）

Week 2:
  Day 6-9:  ACT-03 B1 POC benchmark
  Day 10:   ACT-06 Bloom XXH64 评估

Week 3:
  Go/No-Go 决策会议 — 基于 POC 结果决定是否正式启动 int64 二期
```

---

## 关联信息

- 源会议：meeting_012_int64_feasibility
- 决议文档：[03_decisions.md](03_decisions.md)
- 会议议程：[01_agenda.md](01_agenda.md)
