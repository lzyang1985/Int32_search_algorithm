---
title: 需求对齐文档 — Int64 二期 Phase 2 (COW 多线程 + Bloom 重建)
status: Frozen
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Architect
task_id: task_006_int64_phase2_cow
parent_doc: none
parent_task: task_005_int64_extension
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/tasks/task_005_int64_extension/FINAL_int64_b1.md"
  - "docs/tasks/task_005_int64_extension/TODO_int64_b1.md"
  - "docs/tasks/task_002_phase15_cow/ALIGNMENT_task_002_phase15_cow.md"
  - "docs/meetings/meeting_index/meeting_016_optimization_direction/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_016_optimization_direction/02_discussion.md"
trace_code:
  - "src/api_int64.c"
  - "src/internal_int64.h"
  - "include/int64_search.h"
  - "src/api.c"
  - "src/internal.h"
  - "src/platform_thread.h"
  - "test/test_int64.c"
tags: [alignment, int64, b1, cow, bloom-rebuild, phase2, multithreading]
---

# 需求对齐文档 — Int64 二期 Phase 2 (COW 多线程 + Bloom 重建)

## 1. 原始需求

### 1.1 需求来源

本阶段立项来源为 **meeting_016_optimization_direction**（2026-06-02 终会）的核心决议：

| 决议 | 内容 | 状态 |
|------|------|------|
| **D-116** | Int64 Phase 2 范围: **COW 多线程 + Bloom 重建 + broadcast hoisting** | 5/5 全票 🔒 |
| **D-117** | COW 方案修正: 复用 Int32 逐字段 atomic + reader_count（不用 spinlock） | 4/4 全票 🔒 |
| **D-118** | 当前 rebuild 标注"单线程 only"安全警告（ACT-31 已完成） | 4/4 全票 🔒 |

D-116 验收标准: TSan 零告警、ASan/UBSan 零告警、rebuild 后 bloom 状态保持。

### 1.2 触发背景

Phase 1 审计发现 **DEV-I64-001**（rebuild 不重建 bloom, Minor）和 Phase 1 TODOs 中已记录的 **TODO-10**（COW 原子交换方案设计）、**TODO-11**（bloom 重建逻辑）。Sec 在 meeting_016 中进一步升级了优先级判断：

> **🔴 安全门禁发现**：当前 `int64_search_rebuild()` 直接 `free(impl->vals)`，并发 reader 仍在使用时存在 **use-after-free**。
> 引用 [api_int64.c:127-166](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L127-L166) — `platform_aligned_free(impl->vals)` 在 line 150，无 reader_count 保护。
> 现状与 D-118 一致：单线程 only，文档中已标注（ACT-31 完成）。但这只是缓解措施，根本修复在 Phase 2 COW。

### 1.3 不变量（必须保持）

- **API 兼容**：`int64_search_*` 公开头文件不变（包括 Phase 1 标注的"单线程 only"警告将移除）
- **符号独立**：所有新符号 `int64_` 前缀，与 libint32search 不冲突
- **库独立**：`libint64search.a` 独立静态库，链接方式不变
- **行为兼容**：单线程调用语义与 Phase 1 完全一致

---

## 2. 边界确认（任务范围）

### 2.1 Phase 2 包含

| 模块 | 文件 | 变更类型 | 说明 |
|------|------|----------|------|
| 内部结构 | `src/internal_int64.h` | **修改** | `vals`、`dir`、`n` 改为 `_Atomic`；新增 `reader_count`；`path` 改为 `_Atomic int`（`int32` 是 lock-free 的，零开销） |
| 内部结构 | `src/internal_int64.h` | **修改** | `bloom` 保持 `_Atomic(void *)`（Phase 1 已正确实现） |
| API 层 | `src/api_int64.c` | **修改** | `find()` 改造：reader_count++ / atomic load / search / reader_count-- |
| API 层 | `src/api_int64.c` | **修改** | `rebuild()` 改造：build new → atomic exchange → wait reader_count==0 → free old（**COW**） |
| API 层 | `src/api_int64.c` | **修改** | `rebuild()` **同时重建 bloom**（修复 DEV-I64-001 + TODO-11） |
| API 层 | `src/api_int64.c` | **修改** | `destroy()` 改造：等待 reader_count==0 后释放 |
| 公开头文件 | `include/int64_search.h` | **修改** | 移除"单线程 only"警告注释 |
| 构建系统 | `Makefile` | **修改** | 新增 `test-int64-thread` 目标 |
| 构建系统 | `README.txt` | **修改** | 标注 Int64 多线程安全，更新编译命令 |
| 测试 | `test/test_int64_thread.c` | **新增** | TSan 并发压力测试（2 writer + 4 reader 模板，复用 Int32 模式） |
| 测试 | `test/test_int64.c` | **修改** | L1 增补 COW 行为测试（rebuild 期间 find 正确返回） |

### 2.2 Phase 2 明确不包含

| 不包含项 | 原因 | 归属阶段 |
|----------|------|----------|
| ❌ `int32_t dir` → `int64_t dir` 迁移 | YAGNI（D-127）；n 受限于 INT32_MAX | 等待需求驱动 |
| ❌ `find_range` B1 路径实现 | 等待业务需求（D-128） | 远期 |
| ❌ Phase 1 偏差 DEV-AUDIT-01（test_int64.c cfg 未用） | Minor，已在 ACT-27 关闭 | — |
| ❌ Phase 1 偏差 AUDIT-NOTE-01（build_sorted_int64 溢出） | Minor，已在 ACT-28 关闭 | — |
| ❌ broadcast hoisting 实现 | ACT-29 已完成于 Phase 1 | — |
| ❌ Eytzinger 布局 | `[DEBT-Eytzinger]` 归档（D-121） | 远期 |
| ❌ AVX-512 | `[DEBT-AVX512]` 关闭（D-119） | 关闭 |
| ❌ ARM NEON | `[DEBT-NEON]` 远期（D-120） | 远期 |
| ❌ 热键缓存 POC | 独立 POC（ACT-39） | P1 独立 |
| ❌ Huge Pages POC | 独立 POC（ACT-40） | P1 独立 |
| ❌ RMI | `[DEBT-RMI]` 不推荐（D-123） | 关闭 |

### 2.3 不变更项

- `search_int64_scalar` / `search_int64_b1` — 零修改（搜索函数接受局部快照指针，与原子化无关）
- `build_sorted_int64` / `build_dir_int64` / `build_decision_int64` — 零修改（构建只产出局部快照，不持有 impl）
- `platform_memory.c/h` / `platform_cpu.c/h` — 零修改
- `platform_thread.h` — 零修改（已具备所需原子操作封装，可直接复用）
- `int32_search.*` 所有文件 — 零修改（独立库）
- 错误码 `INT64_SEARCH_*` 当前 5 个 — Phase 2 不增改错误码（见 §4 Q2）

---

## 3. 需求理解（对现有项目的理解）

### 3.1 现有 Int64 Phase 1 的并发缺陷（精确诊断）

| 位置 | 缺陷 | 严重度 | Phase 1 缓解 | Phase 2 修复 |
|------|------|--------|--------------|--------------|
| [api_int64.c:150](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L150) | rebuild 直接 `free(impl->vals)`，无 reader_count 保护 | **HIGH** | D-118 警告注释 | COW + reader_count 等待 |
| [api_int64.c:151](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L151) | rebuild 直接 `free(impl->dir)`，无 reader_count 保护 | **HIGH** | D-118 警告注释 | COW + reader_count 等待 |
| [api_int64.c:141-144](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L141-L144) | rebuild `bloom_destroy` 在新 dir 分配前，无 reader 保护 | **MEDIUM** | D-118 警告注释 | COW + 旧 bloom 延迟释放 |
| [api_int64.c:81](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L81) | find 读 `impl->bloom_bypass` 用 relaxed（正确） | OK | — | 保持 |
| [api_int64.c:94-97](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L94-L97) | find 读 `impl->vals` / `impl->dir` **非原子** | **HIGH** | 单线程 only | 改为 `_Atomic` + acquire load |
| [api_int64.c:108-125](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L108-L125) | destroy 直接 `free(impl->vals)` / `free(impl->dir)`，无 reader_count 等待 | **HIGH** | 单线程 only | 等待 reader_count==0 后释放 |
| [api_int64.c:127-145](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L127-L145) | rebuild 不重建 bloom，旧 bloom 与新数据不一致 | **MEDIUM** (DEV-I64-001) | 未标注 | 同步重建 bloom（COW 框架内） |

### 3.2 Int32 B1 COW 完整参考（已验证模式）

[Int32 src/api.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c) 的 COW 实现是 Phase 2 的直接模板。关键模式：

**内部结构**（[src/internal.h:14-24](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L14-L24)）：
```c
typedef struct {
    _Atomic(const int32_t  *) vals;    // 8 字节，lock-free
    _Atomic(const uint16_t *) lo16;    // 8 字节，lock-free
    _Atomic(const int32_t  *) dir;     // 8 字节，lock-free
    _Atomic size_t            n;        // 8 字节，lock-free
    int                       path;     // 4 字节，写入由 atomic_vals/acquire 保护（详见 Q3）
    int32_t                  (*search_fn)(...);
    uint8_t                   avx2_capable;
    _Atomic size_t            reader_count;  // 8 字节，lock-free
    _Atomic(void *)           bloom;
} int32_search_impl_t;
```

**find() 关键模式**（[src/api.c:137-176](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L137-L176)）：
```c
atomic_size_fetch_add(&impl->reader_count, 1, memory_order_acquire);
const int32_t *v = atomic_ptr_load(&impl->vals, memory_order_acquire);
size_t _n = atomic_size_load(&impl->n, memory_order_acquire);
result = search_fn(v, _n, key, out_index);
atomic_size_fetch_sub(&impl->reader_count, 1, memory_order_release);
```

**rebuild() 关键模式**（[src/api.c:178-277](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L178-L277)）：
```c
// 1. 构造新快照
int32_t *new_vals = build_sort_and_validate(data, n);
int32_t *new_dir  = build_dir(new_vals, n);
uint16_t *new_lo16 = build_b1(new_vals, n, new_dir);

// 2. 原子交换（acq_rel 语义）
atomic_size_store(&impl->n, n, memory_order_release);
old_vals = atomic_ptr_exchange(&impl->vals, new_vals, memory_order_acq_rel);
old_dir  = atomic_ptr_exchange(&impl->dir,  new_dir,  memory_order_acq_rel);
old_lo16 = atomic_ptr_exchange(&impl->lo16, new_lo16, memory_order_acq_rel);

// 3. 等待 reader 退出
while (atomic_size_load(&impl->reader_count, memory_order_acquire) > 0)
    platform_thread_yield();

// 4. 释放旧数据
if (old_vals) platform_aligned_free((void *)old_vals);
if (old_lo16) free((void *)old_lo16);
if (old_dir)  free((void *)old_dir);
```

**Int64 优势**（D-117 已确认）：
- Int64 B1 只有 **2 指针**（vals + dir），比 Int32 B1 少 1 个（lo16）
- 总 atomic 字段：vals (8) + dir (8) + n (8) = 24 字节 → 拆分后 **每个 8 字节 lock-free**
- 无需 spinlock，无需 struct 级 CAS

### 3.3 Int64 Bloom 重建策略

Phase 1 偏差 [DEV-I64-001](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_005_int64_extension/FINAL_int64_b1.md) 描述：
> rebuild() 不重建 bloom。Phase 1 接受（Phase 2 修复）

**Phase 2 修复方案**（Int32 B1 已验证）：
1. 构造 `new_bloom`（若 `old_bloom != NULL`）
2. 原子替换 `atomic_ptr_exchange(&impl->bloom, new_bloom, acq_rel)` → 拿到 `old_bloom`
3. 等待 reader_count==0
4. `bloom_destroy(old_bloom)`（在旧 vals/dir 释放时一并处理）

**关键问题**：bloom_bypass 状态在 rebuild 后必须**保持**（D-110 + Phase 1 DESIGN §2.4 已确认）。`atomic_int` 字段独立于 bloom 指针，不参与交换。

### 3.4 现有 `platform_thread.h` 已具备的能力

[src/platform_thread.h](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/platform_thread.h) 已提供：
- `atomic_ptr_load / atomic_ptr_store / atomic_ptr_exchange`
- `atomic_size_load / atomic_size_store`
- `atomic_size_fetch_add / atomic_size_fetch_sub`
- `platform_thread_yield()` (Linux: `sched_yield()`; Win+x86: `_mm_pause()`; Win+ARM: `Sleep(0)`)

**Phase 2 零新增 PAL 文件。** 完全复用。

### 3.5 与现有 int32 代码库的影响

| 影响 | 说明 |
|------|------|
| 零修改 int32 | Phase 2 所有变更仅在 `*_int64.*` 文件 |
| 零 API 变更 | `int64_search.h` 函数签名不变，仅移除警告注释 |
| 共享 .o | `platform_memory.o` / `platform_cpu.o` / `bloom_filter.o` 链接不变 |
| 符号冲突 | 无。新增字段在 `int64_search_impl_t` 内，类型私有 |

---

## 4. 疑问澄清

### 4.1 已澄清项（来自 D-116 / D-117 / Phase 1 TODO）

| # | 疑问 | 决议 | 来源 |
|---|------|------|------|
| 1 | Phase 2 范围 | COW + Bloom 重建 + broadcast hoisting | D-116 5/5 |
| 2 | COW 方案: spinlock 还是逐字段 atomic? | 逐字段 atomic + reader_count（D-117 修正） | D-117 4/4 |
| 3 | broadcast hoisting 实现 | 已在 ACT-29 完成于 Phase 1 | Phase 1 ACT-29 |
| 4 | bloom_bypass 状态 | rebuild 后保持（`_Atomic int` 独立字段） | Phase 1 DESIGN §2.4 |
| 5 | 旧版本延迟回收 | 等待 reader_count==0 后释放 | D-005 + Phase 1.5 Q1 |
| 6 | 库独立性 | 保持独立 `libint64search.a` | D-093 |

### 4.2 已决议项（来自用户决策 2026-06-04）

| # | 疑问 | 决议 | 影响 |
|---|------|------|------|
| **Q1** | `int64_search_impl_t::path` 是否原子化? | **✅ 改 `_Atomic int`** | reader 端 `atomic_load(path, acquire)`,与 vals/dir/n 同一批 lock-free 字段;消除撕裂读 |
| **Q2** | 错误码是否新增? | **✅ 零新增** | Phase 2 维持 5 个错误码不变;rebuild 失败仅返回 `ERR_MEMORY` |
| **Q3** | destroy() 是否等待 reader? | **✅ 等待 reader_count==0** | destroy 末尾 `while(reader_count>0) platform_thread_yield();`,与 Int32 v1.0.0 一致 |
| **Q4** | TSan 测试规模? | **✅ 1M uniform TSan + 10M 性能回归** | TSan 用例 1M uniform (80MB 内存,合理 CI 占用);10M 留作 ASan 性能回归 |

### 4.3 自动推导项（无需决策）

| # | 决策 | 来源 |
|---|------|------|
| A1 | `reader_count` 保护范围: find() / rebuild() / destroy() 三处全部 acquire 配对 release | D-005 + Q3 决议 |
| A2 | `bloom_bypass` 重建后保持原值 | D-110 + Phase 1 DESIGN §2.4 |
| A3 | `rebuild` 失败时旧数据不受影响(COW 天然优势) | D-005 |
| A4 | 单 writer 假设(多 writer 需外部互斥) | Phase 1 文档约束,与 Int32 一致 |

---

## 5. 性能基线

### 5.1 Phase 1 性能参考（已验证）

| 分布 | 规模 | max_bucket | B1 cy/q | vs Scalar |
|------|------|-----------|---------|-----------|
| uniform | 1M | 8 | 144 | **4.91x** |
| uniform | 10M | 26 | 318 | **5.30x** |
| skewed 80/20 | 1M | 16 | 150 | 4.43x |
| skewed 80/20 | 10M | 71 | 443 | 4.08x |
| zipf α=1.0 | 1M | 69,732 | 2036 → 676 (fallback) | 2.31x |
| zipf α=1.0 | 10M | 692,681 | 29,917 → 1596 (fallback) | -- |

### 5.2 Phase 2 性能目标

| 指标 | 目标值 | 验证方式 |
|------|--------|----------|
| 单线程 find 性能 | **不退化**（与 Phase 1 持平 ±5%） | benchmark 回归（`test_int64_perf.c`） |
| 单线程 rebuild 性能 | **不退化** | benchmark 回归 |
| 多线程 find 线性扩展 | 4 reader 接近 4x throughput | `test_int64_thread.c` |
| 并发 find + rebuild 零崩溃 | TSan 零告警 | `test_int64_thread.c` + TSan |

**性能不退化是核心**：Phase 2 只增加少量原子读开销（一次 `atomic_size_fetch_add` 约 1-2 ns），查询热路径应在噪声范围内。

### 5.3 内存占用

| 组件 | 大小 | 备注 |
|------|------|------|
| vals (10M int64) | 80 MB | 不变 |
| dir[1048577] int32_t | 4 MB | 不变 |
| bloom (可选) | ~1-2 MB | 不变 |
| reader_count atomic | 8 字节 | 新增（无开销） |
| atomic 字段开销 | 0 | 字段已在原 struct 内，仅类型变更 |
| **rebuild 期间临时内存** | **+80MB vals + 4MB dir** | 旧 + 新同时存在，等待 reader 退出 |
| **峰值内存** | **~165 MB** | 仅 rebuild 瞬时 |

---

## 6. 验收标准（D-116 明确）

### 6.1 必须满足

- [ ] TSan 编译（`-fsanitize=thread`）下 `test_int64_thread.c` 零告警
- [ ] ASan/UBSan 编译下 Phase 1 所有测试继续通过
- [ ] `int64_search_rebuild()` 期间并发 `find()` 全部正确返回（不崩溃、不返回错位索引）
- [ ] `int64_search_destroy()` 等待 reader 退出后释放（无 use-after-free）
- [ ] `int64_search_rebuild()` 后 bloom_bypass 状态保持
- [ ] `int64_search_rebuild()` 后 bloom 与新数据一致（**修复 DEV-I64-001**）

### 6.2 性能验收

- [ ] 单线程 find 性能不退化（±5%）
- [ ] 单线程 rebuild 性能不退化（±10%）
- [ ] 4 reader 并发 find 吞吐 ≥ 3.5x 单线程

### 6.3 文档验收

- [ ] `include/int64_search.h` 移除"单线程 only"警告注释
- [ ] `README.txt` 标注 Int64 多线程安全
- [ ] `test/test_int64_thread.c` 存在且可被 TSan 编译
- [ ] `Makefile` 含 `test-int64-thread` 目标

---

## 7. 与现有代码的兼容性分析

### 7.1 需要变更的文件

| 文件 | 变更内容 | 行数估算 |
|------|----------|----------|
| `include/int64_search.h` | 移除"单线程 only"注释块（3 行删除） | -3 |
| `src/internal_int64.h` | `vals`/`dir`/`n` 改 `_Atomic`；新增 `reader_count`；`path` 改 `_Atomic int`（Q1 决策后） | +5 ~ +10 |
| `src/api_int64.c` | `find()` 改造（reader_count）；`rebuild()` 改造（COW + bloom 重建）；`destroy()` 改造（wait reader） | +50 ~ +80, ~30 修改 |
| `test/test_int64_thread.c` | **新增**（参考 [test/test_thread.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/test_thread.c)） | +120 ~ +150 |
| `test/test_int64.c` | L1 增补 COW 行为测试 | +30 |
| `Makefile` | 新增 `test-int64-thread` 目标 | +5 |
| `README.txt` | 标注 Int64 多线程安全 + 更新编译命令 | +5 |

### 7.2 不影响的部分

- 所有 `search_int64_*` 文件 — 零修改
- 所有 `build_*_int64` 文件 — 零修改
- `platform_memory.c/h`、`platform_cpu.c/h`、`platform_thread.h`、`bloom_filter.c/h` — 零修改
- `int32_search.*` — 零修改
- Phase 1 所有测试（L1-L7）— 单线程行为不变，全部应继续通过

### 7.3 ABI 兼容性

- `int64_search_impl_t` 私有，外部通过 `void*` 访问
- `_Atomic` 修饰对 ABI 无影响（lock-free 类型保持相同大小/对齐）
- 公开头文件仅删除注释，无 API 签名变更

---

## 8. 风险评估

| 风险 | 等级 | 缓解措施 |
|------|------|----------|
| `path` 字段原子化引入额外开销 | 低 | `_Atomic int` 在 x86-64 上是 lock-free 零开销，0-1 个额外 cycle |
| rebuild 峰值内存 ~165MB | 中 | 测试仅 1M（~17MB 峰值），10M 留作性能回归 |
| TSan 在 AVX2 intrinsic 上误报 | 中 | 复用 Int32 测试模板的 workaround（`-fsanitize=thread -O1`） |
| 旧 Int64 用户依赖"单线程 only"行为 | 低 | 警告文档移除，但语义强化（更安全），向后兼容 |
| bloom 重建在 n=10M 时 O(n) 慢 | 低 | 10M bloom 构建 < 1s（XXH32 单核），可接受 |

---

## 9. 关联信息

- **需求基线**：[总需求文档 §5](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/requirements/总需求文档.md)
- **技术路线**：[技术路线 §5 并发模型](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/architecture/技术路线.md)
- **Phase 1 总结**：[FINAL_int64_b1.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_005_int64_extension/FINAL_int64_b1.md)
- **Phase 1 TODO**：[TODO_int64_b1.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_005_int64_extension/TODO_int64_b1.md)（TODO-10/11 即将在 Phase 2 关闭）
- **决议来源**：[meeting_016 D-116/D-117/D-118](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_016_optimization_direction/03_decisions.md)
- **Int32 Phase 1.5 COW 模板**：[ALIGNMENT_task_002_phase15_cow.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_002_phase15_cow/ALIGNMENT_task_002_phase15_cow.md)
- **Int32 B1 COW 完整参考**：[src/api.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c)
- **对应行动项**：ACT-38（[04_action_items.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_016_optimization_direction/04_action_items.md)）
