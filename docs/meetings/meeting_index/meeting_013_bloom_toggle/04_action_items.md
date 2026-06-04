---
title: 行动项 — 布隆过滤器开关特性
meeting_id: meeting_013_bloom_toggle
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 行动项 — 布隆过滤器开关特性讨论会议

## 📋 行动项总览

| 编号 | 行动项 | 优先级 | 依赖 | 状态 |
|------|--------|--------|------|------|
| ACT-07 | 实现 bloom_bypass 特性（内部字段 + setter + find 热路径） | P0 | 无 | Pending |
| ACT-08 | 更新头文件 `int32_search.h` API 文档 | P0 | ACT-07 | Pending |
| ACT-09 | 测试更新：bloom_bypass 正确性 + 线程安全 + 性能 | P1 | ACT-07 | Pending |
| ACT-10 | 修复 SEC-B1：rebuild() bloom 交换顺序 → 假阴性窗口 | P1 | 无 | Pending |
| ACT-11 | int64 扩展预留：impl_t 字段 + API 骨架 | P2 | ACT-07 | Pending |
| ACT-12 | 修复 SEC-B2：impl->path 原子化 | P2 | 无 | Pending |
| ACT-13 | 性能验证：Path B1 1M bloom vs bypass 延迟对比 | P3 | ACT-07, ACT-09 | Pending |

---

## 行动项详情

### ACT-07：实现 bloom_bypass 特性 [P0]

**目标**：实现方案 C'（内部字段 + setter + 热路径修改）

**交付物**：
1. `src/internal.h`：`int32_search_impl_t` 新增 `_Atomic(int) bloom_bypass` 字段
2. `src/api.c`：
   - `create()` 中初始化 `atomic_init(&impl->bloom_bypass, 0)`
   - `find()` 热路径增加 `!atomic_load(&impl->bloom_bypass, memory_order_relaxed)` 判断
   - 新增 `int32_search_set_bloom_bypass()` 函数
   - rebuild() 保持 bloom_bypass 状态不变
3. `include/int32_search.h`：新增函数声明 + 完整文档注释
4. 源代码编译通过（`make`）

**验收标准**：
- `create(..., cfg.use_bloom=1)` 后 `bloom_bypass=0`（默认）
- `set_bloom_bypass(handle, 1)` 后 find() 不调用 bloom_query（可通过 debug 日志验证）
- `set_bloom_bypass(handle, 0)` 后恢复 bloom 预筛
- NULL handle 返回 ERR_NULL_HANDLE
- 无 bloom 句柄上 set bypass 为安全 no-op

---

### ACT-08：更新头文件 API 文档 [P0]

**目标**：在 `int32_search.h` 中为新增函数编写完整文档注释

**交付物**：
1. `int32_search_set_bloom_bypass()` 完整 doxygen 风格注释（含使用场景、注意事项）
2. `int32_search_config_t.use_bloom` 注释中增加"运行时可通过 set_bloom_bypass() 临时绕过"的说明
3. README.txt 新增 bloom bypass 使用示例

---

### ACT-09：测试更新 [P1]

**目标**：覆盖 bloom_bypass 的所有关键路径

**交付物**（按 Fullstack 的 4 层测试策略）：

| 层 | 测试内容 | 用例数 |
|----|---------|--------|
| L1 接口契约 | NULL handle、非法参数、no-bloom 句柄上调用 | 3 |
| L2 正确性等价 | bloom 启用/绕过 两种模式下搜索结果完全一致 | 2 |
| L3 并发安全 | 多线程 bypass 切换 + 并发查询不崩溃 | 2 |
| L4 性能差异化 | 确知存在场景下 bypass 延迟显著低于正常模式 | 1 |

**总新增用例**：~8 个

---

### ACT-10：修复 SEC-B1 rebuild() bloom 交换顺序 [P1]

**目标**：消除 rebuild() 中 bloom 交换滞后于数据交换导致的瞬时假阴性窗口

**问题描述**：当前 `rebuild()` 先交换 vals/lo16/dir，后交换 bloom。读者在此窗口中看到新数据+旧bloom，可能对"确定在新数据中但旧bloom标记为不存在"的 key 产生假阴性。

**交付物**：
1. 将 bloom 原子交换移到数据交换之前
2. 确保 memory_order 正确（使用 acq_rel + 必要时 seq_cst fence）
3. 验证修复后无 TSan 告警

**验收标准**：rebuild 后所有 key 查询结果与数据一致，无假阴性

---

### ACT-11：int64 扩展预留 [P2]

**目标**：在 int64 扩展设计文档中预留 bloom_bypass 字段和 API

**交付物**：
1. 更新 `docs/architecture/技术路线.md` §9 中的 int64 API 骨架，包含 `int64_search_set_bloom_bypass()`
2. `int64_search_impl_t` 设计文档中包含 `_Atomic(int) bloom_bypass` 字段
3. 确认 int64 bloom 迁移时的 XXH64 和溢出检查需求（参考 Security I64-01, I64-02）

---

### ACT-12：修复 SEC-B2 impl->path 原子化 [P2]

**目标**：消除 `impl->path` 的非原子并发读写（C11 数据竞争）

**交付物**：
1. `src/internal.h`：`int path` 改为 `_Atomic(int) path`
2. `src/api.c`：所有 `impl->path` 读写改为 `atomic_load/store`
3. 验证 TSan 零告警

**验收标准**：TSan 零数据竞争

---

### ACT-13：性能验证 [P3]

**目标**：量化 bloom_bypass 的实际性能收益

**交付物**：
1. 基准测试：1M uniform 数据，100% hit（确知存在），对比 bloom_bypass=0 vs 1
2. 记录 cycles/query 差异
3. Path B1 领域预期收益 ~50 cy（~70%）

---

## 📅 建议执行顺序

```
Day 1:
  ACT-07  bloom_bypass 实现（api.c/internal.h/int32_search.h）
  ACT-08  头文件文档更新（与 ACT-07 同步）

Day 2:
  ACT-09  测试更新（L1+L2 优先）
  ACT-10  SEC-B1 修复（bloom 交换顺序）

Day 3:
  ACT-12  SEC-B2 修复（path 原子化）
  ACT-13  性能验证

Day 4:
  ACT-11  int64 扩展预留
```

---

## 关联信息

- 源会议：meeting_013_bloom_toggle
- 决议文档：[03_decisions.md](03_decisions.md)
- 会议议程：[01_agenda.md](01_agenda.md)
- 讨论记录：[02_discussion.md](02_discussion.md)
