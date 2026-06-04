---
title: 决议记录 — 布隆过滤器开关特性
meeting_id: meeting_013_bloom_toggle
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 决议记录 — 布隆过滤器开关特性讨论会议

## 📋 决议总览

| 决议编号 | 议题 | 结论 | 票数 |
|----------|------|------|------|
| D-097 | 是否提供 Bloom 旁路能力 | ✅ Go — 提供 | 5/5 |
| D-098 | 采用方案 | 方案 C'（内部 _Atomic(int) bloom_bypass + setter） | 5/5 |
| D-099 | API 设计 | 新增 `int32_search_set_bloom_bypass()` + `_Atomic(int) bloom_bypass` | 5/5 |
| D-100 | int64 同步支持 | libint64search v0.1.0 即内置，API 命名一致 | 5/5 |
| D-101 | 并发问题处置 | Security 审计发现的 P1 问题纳入 Phase 3 TODO | 5/5 |

---

## D-097：是否提供 Bloom 旁路能力

**结论：✅ Go — 提供（5/5）**

**理由**：
1. Algorithm 量化分析：Bloom 预筛实际开销约 50 cycles（XXH32×3=24cy + 取模×3=15cy + 位检查L3=12cy），Path B1 下占比最高达 70%，"确定存在"场景为纯浪费
2. Backend 确认：同一句柄在"写流量（多miss）"与"回放流量（多hit）"之间切换是真实生产场景，create-time 配置无法覆盖
3. Architect 确认：Bloom 是独立预筛层，提供旁路是补充缺失的语义表达，不否认 Bloom 本身的价值

**风险**：低。Bloom 旁路不改变任何现有行为，纯增量特性。

---

## D-098：采用方案 C' — 内部字段 + setter

**结论：✅ 采用方案 C'（5/5，经冲突交叉讨论达成一致）**

**方案 C' 核心设计**：

1. **不修改 `int32_search_config_t`**：保持 cfg 为纯构建配置，`use_bloom` 语义不变
2. **在 `int32_search_impl_t` 中新增 `_Atomic(int) bloom_bypass`**：初始值 0（不绕过）
3. **新增 `int32_search_set_bloom_bypass(handle, bypass)` 函数**：运行时 setter，仅修改原子字段，不触发内存管理
4. **`find()` 热路径增加 `!impl->bloom_bypass` 条件**：`memory_order_relaxed` 加载，~1 cycle 开销
5. **rebuild() 保持 bloom_bypass 状态不变**：作为句柄属性持久化
6. **`find_range()` 保留现状**：当前不使用 bloom，不受影响

**被排除的方案**：

| 方案 | 被排除原因 |
|------|-----------|
| A (find_no_bloom) | API 膨胀，函数名爆炸风险，与 find_range 需要同步提供 |
| B (find_ex + flags) | 破坏 ABI，所有调用点需修改，对 v1.0.0-rc 代价过大 |
| D (不提供) | 放弃真实性能收益（Path B1 下 70% 查询延迟可消除） |
| C (cfg 字段) | 关注点分离违反（构建配置 vs 查询策略混淆） |

---

## D-099：API 设计

**结论：✅ 新增 API（5/5）**

### 头文件变更（`include/int32_search.h`）

```c
/**
 * 设置布隆过滤器旁路模式（运行时切换，线程安全）
 *
 * 当 bypass=1 时，所有 int32_search_find() 调用将跳过 bloom 预筛步骤，
 * 直接进入搜索路径。这对于"确定 key 一定存在"的批量查询场景可显著
 * 提升性能（Path B1 下可减少最高 70% 的查询延迟）。
 *
 * 注意：
 *   - 仅当句柄通过 cfg.use_bloom=1 创建了 bloom 时，此设置才有意义
 *   - 无 bloom 的句柄上调用为安全 no-op
 *   - 不保证在已发起的并发 find() 调用中即时生效
 *   - rebuild() 不改变此设置
 *
 * @param handle  int32_search_create 返回的句柄
 * @param bypass  0=正常使用bloom预筛（默认），1=跳过bloom直接搜索
 * @return       INT32_SEARCH_OK 或 ERR_NULL_HANDLE
 */
int int32_search_set_bloom_bypass(int32_search_t handle, int bypass);
```

### 内部结构变更（`src/internal.h`）

```c
typedef struct {
    // ... 现有字段不变 ...
    _Atomic(void *) bloom;
    _Atomic(int)    bloom_bypass;   // 新增：0=正常，1=旁路
} int32_search_impl_t;
```

### 热路径变更（`src/api.c` find()）

```c
// 原：
if (bf != NULL && !bloom_query(bf, key)) { return NOT_FOUND; }

// 新：
int _bypass = atomic_load(&impl->bloom_bypass, memory_order_relaxed);
if (!_bypass && bf != NULL && !bloom_query(bf, key)) { return NOT_FOUND; }
```

### 兼容性

| 维度 | 状态 |
|------|------|
| 源码兼容 | ✅ 完全兼容（不改变任何现有签名） |
| 二进制兼容 | ✅ 完全兼容（纯增量） |
| cfg 兼容 | ✅ cfg.use_bloom 语义不变 |
| COW 兼容 | ✅ 零冲突（relaxed atomic，不操作 bloom 指针） |

---

## D-100：int64 扩展同步支持

**结论：✅ 同步支持（5/5）**

| 决策 | 内容 |
|------|------|
| 同步时机 | libint64search v0.1.0 即内置此特性 |
| API 命名 | `int64_search_set_bloom_bypass()` — 与 int32 保持一致 |
| 实现策略 | infrafirst：int32 先实现并验证 → int64 模式复制（pattern replication） |
| 数据结构 | `int64_search_impl_t` 预留 `_Atomic(int) bloom_bypass` 字段 |

独立库模式下不共享代码，但保持设计模式一致。

---

## D-101：并发问题处置

**结论：✅ 纳入 Phase 3 TODO（5/5）**

Security 审计发现 3 个独立于本次议题的并发问题，处置如下：

| 编号 | 问题 | 严重度 | 处置 |
|------|------|--------|------|
| SEC-B1 | rebuild() bloom 交换顺序 → 瞬时假阴性窗口 | P1/中高危 | 纳入 Phase 3 TODO，下一任务修复 |
| SEC-B2 | impl->path 非原子读写（C11 数据竞争） | P2/中危 | 纳入 Phase 3 TODO，下一任务修复 |
| SEC-B3 | B1 三指针非原子批量交换 | P3/信息级 | 已知设计权衡，已有文档记录 |

> 这三个问题为 Security 在本次会议审查中的附带发现，不影响 D-097~D-100 决议的执行。

---

## 📊 决议执行优先级

| 优先级 | 决议 | 行动 |
|--------|------|------|
| P0 | D-097, D-098, D-099 | 实现 bloom_bypass 特性（api.c + internal.h + int32_search.h） |
| P0 | D-099 | 更新头文件 API 文档 |
| P1 | D-099 | 测试更新（4层测试策略） |
| P1 | D-101 | SEC-B1 修复（bloom 交换顺序） |
| P2 | D-100 | int64 扩展预留（impl_t 字段 + API 骨架） |
| P2 | D-101 | SEC-B2 修复（path 原子化） |
| P3 | — | 性能验证（Path B1 1M: bloom vs bypass 延迟对比） |
