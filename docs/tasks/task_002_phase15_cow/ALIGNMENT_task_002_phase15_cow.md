---
title: 需求对齐文档 — Phase 1.5 多线程 COW (Path A)
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Architect
task_id: task_002_phase15_cow
parent_doc: none
parent_task: task_001_phase1_mvp
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/tasks/task_001_phase1_mvp/DESIGN_task_001_phase1_mvp.md"
  - "docs/meetings/meeting_index/meeting_003_implementation_planning/03_decisions.md"
trace_code:
  - "src/api.c"
  - "src/internal.h"
  - "include/int32_search.h"
tags: [alignment, requirements, phase15, cow, multithreading, path-a]
---

# 需求对齐文档 — Phase 1.5 多线程 COW (Path A)

## 1. 原始需求

### 1.1 来源

本阶段来源于 总需求文档 §5「三阶段交付计划」中插队的 Phase 1.5：

```
Phase 1.5: v0.2 — 多线程（Path A COW）
  ├── 新增：FR-04 数据重建（Path A COW，原子指针交换）
  ├── 新增：platform_thread.c（原子操作封装）
  └── 验收：ThreadSanitizer 零告警
```

以及 总需求文档 §6.2「Phase 1.5 多线程验收」：

| 验收项 | 说明 |
|--------|------|
| Path A COW 原子指针交换 | `_Atomic const int32_t*` 正确同步 |
| `int32_search_rebuild()` 不阻塞并发查询 | rebuild 期间 find 继续服务 |
| ThreadSanitizer 并发压力测试零告警 | 数据竞争零容忍 |
| 旧版本延迟回收无内存泄漏 | 旧 vals 数组在安全时点释放 |

### 1.2 背景

Phase 1 MVP 已交付单线程 Path A AVX2 查找。Phase 2（A+B1 双路径）原计划直接实现，但当时遗漏了多线程需求，导致 Phase 1.5 被插入。由于 Phase 2 的 B1 COW（struct 级三指针原子交换）依赖 COW 基础机制，Phase 1.5 应先于 Phase 2 执行。

---

## 2. 边界确认（任务范围）

### 2.1 Phase 1.5 包含

| 模块 | 文件 | 变更类型 | 说明 |
|------|------|----------|------|
| 平台抽象层 | `src/platform_thread.h` | **新增** | C11 原子操作封装（`atomic_ptr_load`/`atomic_ptr_store`/`atomic_ptr_exchange`） |
| 平台抽象层 | `src/platform_thread.c` | **新增** | 平台线程安全原语实现（含读写计数 + 同步等待） |
| 内部结构 | `src/internal.h` | **修改** | `vals` 字段改为 `_Atomic int32_t*`；新增 COW 辅助字段（旧指针暂存、读写计数） |
| API 层 | `src/api.c` | **修改** | 新增 `int32_search_rebuild()`；`find()` 增加原子读取 vals；`destroy()` 适配 COW 清理 |
| 公开头文件 | `include/int32_search.h` | **修改** | 新增 `int32_search_rebuild()` 声明 + 错误码 |
| 构建系统 | `Makefile` | **修改** | 新增 `platform_thread.o` 编译规则 |
| 构建系统 | `README.txt` | **修改** | 更新编译命令 |
| 测试 | `test/test_thread.c` | **新增** | 并发读写压力测试（ThreadSanitizer 验证） |

### 2.2 Phase 1.5 明确不包含

| 不包含项 | 原因 | 归属阶段 |
|----------|------|----------|
| B1 路径 COW（struct 级三指针原子交换） | 依赖 B1 路径本身 | Phase 2 |
| B1 路径实现（`build_dir.c`/`build_decision.c`/`build_b1.c`/`search_b1.c`） | 属于 Phase 2 核心交付 | Phase 2 |
| 引用计数 / Hazard Pointer 复杂回收 | Phase 1.5 用读写计数 + 等待，足够简单正确 | — |
| Windows SRWLOCK 方案 | 先做 Linux C11 atomic，Windows 方案在 Phase 3 | Phase 3 |
| 多线程 benchmark | 正确性优先，性能数据 Phase 2 补 | Phase 2 |

### 2.3 不变更项

| 项目 | 说明 |
|------|------|
| 查询算法 | `search_avx2_find` / `search_scalar_find` 代码零修改 |
| 构建逻辑 | `build_sort_and_validate` 代码零修改 |
| `int32_search_create` 语义 | 仍为单线程调用（多线程 create 不在范围内） |
| `int32_search_destroy` 语义 | 增强：等待所有活跃 reader 退出后再释放 |
| API 错误码 | 扩增一个 `INT32_SEARCH_ERR_AGAIN`（rebuild 并发冲突），其余不变 |
| 公开头文件其他 API | `find`/`find_range`/`version` 声明不变 |

---

## 3. 需求理解

### 3.1 并发模型：构建-查询分离 + COW

```
Writer 线程（rebuild）                Reader 线程（find）
─────────────────────                ────────────────────
build_sort_and_validate(data, n)     
  → new_vals (新排序数组)             
                                     atomic_load(&impl->vals, acquire) → v
atomic_store(&impl->vals, new_vals, release)
                                     使用 v 执行 AVX2/标量二分查找
等待所有活跃 reader 退出
platform_aligned_free(old_vals)
```

关键约束（来自 技术路线 §5）：
- Writer: `memory_order_release` — 确保新数组内容在指针更新前对读者可见
- Reader: `memory_order_acquire` — 确保读取指针后能看到完整的新数组内容
- 查询路径零锁 — `find()` 内部无互斥锁

### 3.2 旧版本延迟回收策略

需要回答的核心问题：**何时安全释放旧 `vals` 数组？**

方案对比：

| 方案 | 复杂度 | 正确性 | 内存效率 | 延迟 |
|------|--------|--------|----------|------|
| A. 读写计数 + 等待 | 低 | ✅ | 高（及时释放） | rebuild 尾段短暂等待 |
| B. 仅在下一次 rebuild 时释放 | 极低 | ❌（仍可能 use-after-free） | 中 | 无等待 |
| C. Epoch-based reclamation | 高 | ✅ | 高 | 有 epoch 延迟 |
| D. 引用计数（每 vals 一个） | 中 | ✅ | 高 | 无等待，但原子开销大 |

**推荐方案 A**：读写计数 + 等待。理由：
- C11 `stdatomic.h` 直接支持
- 逻辑简单，ThreadSanitizer 友好
- `rebuild` 是低频操作，尾段短暂等待可接受
- 不需要额外的 epoch 线程或复杂数据结构

具体机制：
1. `impl` 内维护 `atomic_size_t reader_count`
2. `find()` 进入时 `fetch_add(1, acquire)`，退出时 `fetch_sub(1, release)`
3. `rebuild()` 交换 vals 后，自旋等待 `reader_count == 0`，然后释放旧 vals

### 3.3 `int32_search_rebuild` 接口设计

```c
/**
 * 重建查询数据（COW 方式，不阻塞并发查询）
 *
 * @param handle  int32_search_create 返回的句柄
 * @param data    新的 Int32 数组（调用方所有，不修改）
 * @param n       新数组长度 (>0)
 * @return        INT32_SEARCH_OK (成功)
 *                ERR_NULL_HANDLE (handle==NULL)
 *                ERR_MEMORY (内存不足)
 *                ERR_INVALID_ARG (data==NULL || n==0)
 *
 * 行为：
 *   - 创建 new_vals = build_sort_and_validate(data, n)
 *   - 原子替换 impl->vals → new_vals
 *   - 等待所有活跃 reader 退出后释放旧 impl->vals
 *   - 更新 impl->n = n
 *   - 不阻塞并发 find() 调用
 *
 * 限制：
 *   - 不支持并发 rebuild（多 writer 需外部同步）
 *   - rebuild 期间 create/destroy 仍非线程安全
 */
int int32_search_rebuild(int32_search_t handle,
                          const int32_t *data, size_t n);
```

### 3.4 出错语义

| 错误码 | 值 | 触发条件 |
|--------|-----|----------|
| `INT32_SEARCH_OK` | 0 | rebuild 成功 |
| `INT32_SEARCH_ERR_NULL_HANDLE` | -2 | handle == NULL |
| `INT32_SEARCH_ERR_MEMORY` | -3 | 新数据排序/分配失败（旧数据不受影响） |
| `INT32_SEARCH_ERR_INVALID_ARG` | -4 | data==NULL 或 n==0 |

注意：rebuild 失败时**旧数据不受影响**，handle 仍然可用。这是 COW 的天然优势。

### 3.5 `int32_search_find` 并发安全改造

**当前代码**（`api.c:67-78`）：
```c
int32_search_impl_t *impl = (int32_search_impl_t *)handle;
return impl->search_fn(impl->vals, impl->n, key, out_index);
```

**改造后**：
```c
int32_search_impl_t *impl = (int32_search_impl_t *)handle;
atomic_fetch_add_explicit(&impl->reader_count, 1, memory_order_acquire);
int32_t *v = atomic_load_explicit(&impl->vals, memory_order_acquire);
size_t n = impl->n;  // n 在 rebuild 中的更新在 vals 交换之后
int32_t result = impl->search_fn(v, n, key, out_index);
atomic_fetch_sub_explicit(&impl->reader_count, 1, memory_order_release);
return result;
```

关键点：
1. `reader_count++` 在读取 `vals` **之前**（acquire 语义确保后续读不被重排到此之前）
2. `vals` 原子读取（acquire 与 writer 的 release 配对）
3. `reader_count--` 在 search 完成**之后**（release 语义确保之前的读写不被重排到此之后）

### 3.6 `int32_search_destroy` 并发安全改造

销毁时必须等待所有活跃 reader 退出：
```c
// 等待所有 reader 退出
while (atomic_load_explicit(&impl->reader_count, memory_order_acquire) > 0) {
    // spin 或 sched_yield
}
// 现在安全释放
platform_aligned_free(impl->vals);
free(impl);
```

---

## 4. 疑问澄清

以下问题需要在进入 Architect 阶段前确认：

### Q1: 旧版本回收 — 自旋等待还是让步等待？

`rebuild()` 和 `destroy()` 在等待 `reader_count` 归零时：
- **自旋等待**：`while(reader_count>0);` — 适合读者临界区极短（<100ns）的场景
- **让步等待**：`while(reader_count>0) thrd_yield();` — 避免浪费 CPU

**✅ 决议**：自旋等待。临界区极短（~50ns），`thrd_yield()` 系统调用开销更大。同时在 `platform_thread.h` 提供 `platform_thread_yield()` 包装以备将来需要。

### Q2: `impl->n` 的读写是否需要原子？

`n` 在 `rebuild()` 中随 `vals` 一起更新。`find()` 中先读 `vals`（原子），再读 `n`（非原子）。
- 如果 `n` 的更新在 `vals` 原子交换**之后**（同一 writer 线程），且 reader 先读到新 `vals` 后读到旧 `n`（新 vals 可能比旧 vals 长），则可能越界。
- **解决方案**：`n` 在 `vals` 原子交换**之前**更新，或 `n` 也用原子。由于 writer 是单线程且 `acquire/release` 保证了可见性顺序，直接在 `atomic_store(vals)` 前更新 `n` 即可 —— 但需要确保编译器不对 `n` 的写重排序。

**✅ 决议**：`n` 也改为 `_Atomic size_t`，与 `vals` 一起原子更新。一行改动，彻底消除隐患。C11 `_Atomic size_t` 是 lock-free 的，零开销。

### Q3: `avx2_capable` / `search_fn` 是否需要在 rebuild 中重新评估？

`rebuild` 替换数据但数据集特性不变（仍是排序数组），查询引擎选择逻辑不变。
**✅ 决议**：不重新评估。保持 `create` 时的选择。如需变更引擎，调用方应 `destroy` + `create`。

### Q4: `rebuild` 与 `destroy` 的互斥

如果线程 A 执行 `rebuild`，线程 B 同时执行 `destroy`，可能发生：
- `rebuild` 原子交换了 vals → `destroy` 释放了新 vals → `rebuild` 后续释放旧 vals 时 double-free
- 或者：`destroy` 释放 impl → `rebuild` 访问已释放的 impl

**✅ 决议**：调用方保证互斥。`create`/`destroy`/`rebuild` 三者不可并发，文档中明确标注。这是 Phase 1 已存在的约束，不额外加锁。

---

## 5. 与现有代码的兼容性分析

### 5.1 需要变更的文件

| 文件 | 变更内容 | 行数估算 |
|------|----------|----------|
| `include/int32_search.h` | 新增 `int32_search_rebuild()` 声明 | +10 |
| `src/internal.h` | `vals` → `_Atomic`；新增 `reader_count` | +5 |
| `src/api.c` | 新增 `rebuild()`；改造 `find()`/`destroy()` | +60, ~20 修改 |
| `src/platform_thread.h` | **新增文件** | +25 |
| `src/platform_thread.c` | **新增文件** | +35 |
| `test/test_thread.c` | **新增文件** | +80 |
| `Makefile` | 新增编译规则 | +5 |
| `README.txt` | 更新编译命令 | +5 |

### 5.2 不影响的部分

- `search_avx2.c` / `search_scalar.c` — 零修改
- `build_sorted.c` — 零修改
- `platform_memory.c` / `platform_cpu.c` — 零修改
- 所有现有测试 — 单线程行为不变，全部应继续通过

### 5.3 ABI 兼容性

`int32_search_impl_t` 是内部结构，不暴露给调用方（通过不透明句柄 `void*` 访问）。新增字段不会破坏 ABI。

---

## 6. 关联信息

- 前置任务：[task_001_phase1_mvp](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/task_README.md) — Phase 1 MVP（已完成）
- 后续任务：task_003_phase2_ab1 — Phase 2 A+B1 双路径（待启动）
- 源需求：[总需求文档 §5](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/requirements/总需求文档.md#L114-L136)
- 技术基础：[技术路线 §5 并发模型](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/architecture/技术路线.md#L247-L262)
