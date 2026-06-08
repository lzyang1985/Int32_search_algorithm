---
title: T2+T3 验收文档 — find() 与 destroy() 并发改造
task_id: task_006_int64_phase2_cow
tasks: [T2, T3]
status: SUCCESS
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Executor
parent_task: task_006_int64_phase2_cow
parent_doc: "docs/tasks/task_006_int64_phase2_cow/TASK_task_006_int64_phase2_cow.md"
---

# T2 + T3 验收文档 — find() 与 destroy() 并发改造

## 1. 任务概况

| 字段 | T2 (find) | T3 (destroy) |
|------|-----------|--------------|
| 任务 ID | T2 | T3 |
| 优先级 | P0 | P0 |
| 风险等级 | 高 | 低 |
| 估算 | 1 小时 | 20 分钟 |
| 实际 | ~25 分钟 | ~10 分钟 |
| 并行实施 | ✅ 与 T3 同时 | ✅ 与 T2 同时 |
| 状态 | ✅ **SUCCESS** | ✅ **SUCCESS** |

## 2. 输入契约履行

### T2
- **前置依赖**: T1（字段已 _Atomic 化）✅
- **输入数据**: `src/api_int64.c:73-106`（Phase 1 find 实现）✅
- **环境依赖**: `_Atomic` 字段（来自 T1）✅

### T3
- **前置依赖**: T1 ✅
- **输入数据**: `src/api_int64.c:108-125`（Phase 1 destroy 实现）✅
- **环境依赖**: `platform_thread.h::platform_thread_yield` ✅

## 3. 输出契约履行

### 3.1 T2 find() 验收清单

- [x] **5 步严格顺序**（与 DESIGN §2.4.1 一致）
  1. `fetch_add(reader_count, +1, acquire)` ← 临界区开始
  2. `load(path/n/vals/dir, acquire)` ← 数据快照
  3. bloom 预筛（保持 Phase 1 行为）
  4. 分派 search_int64_b1/scalar
  5. `fetch_sub(reader_count, -1, release)` ← 临界区结束
- [x] **所有错误路径先 fetch_sub 再 return**
  - bloom 预筛失败路径: 先 `fetch_sub(release)` 再 return NOT_FOUND
  - 成功路径: 先 `fetch_sub(release)` 再处理 out_index
- [x] **out_index == NULL 不应崩溃**（保持 Phase 1 行为）
- [x] **多线程并发零警告编译**（`gcc -O3 -std=c11 -Wall -Wextra -mavx2`）

### 3.2 T3 destroy() 验收清单

- [x] **`handle == NULL` 幂等返回 OK**（保持 Phase 1 行为）
- [x] **Q3 决议落地**: 等待 `reader_count == 0` 后释放
  - `while (atomic_load(reader_count, acquire) > 0) platform_thread_yield();`
- [x] **释放顺序**: bloom → vals → dir → memset(0) → free
- [x] **memset(0) 防悬垂指针**（防御性）
- [x] **零警告编译**

### 3.3 修改清单

| 文件 | 类型 | 变更 |
|------|------|------|
| [src/api_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c) | 修改 | +1 include, +30 find, +12 destroy |
| [test/t23_thread_smoke.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/t23_thread_smoke.c) | **新增** | 4 线程 50000 find 冒烟测试 |

## 4. 单任务验证

### 4.1 编译验证

```bash
$ mingw32-make lib-int64
gcc -c -O3 -std=c11 -Wall -Wextra -mavx2 -Iinclude -Isrc src/api_int64.c -o src/api_int64.o
ar rcs libint64search.a src/platform_memory.o src/build_sorted_int64.o src/search_scalar_int64.o \
    src/build_dir_int64.o src/build_decision_int64.o src/search_b1_int64.o src/api_int64.o
EXIT=0
```

✅ 零警告编译
✅ 库重新打包成功

### 4.2 单线程行为回归（T1 sanity test）

```
$ ./t1_sanity.exe
T1 sanity test PASSED
```

| 测试 | Phase 1 行为 | T2+T3 后 |
|------|-------------|----------|
| create | OK | ✅ OK |
| find(existing) | idx=5000 | ✅ idx=5000 |
| find(missing) | NOT_FOUND | ✅ NOT_FOUND |
| rebuild | OK | ✅ OK |
| find(after rebuild) | idx=5000 | ✅ idx=5000 |
| destroy | OK | ✅ OK |

✅ 单线程行为零退化（C11 `_Atomic` 普通赋值兼容，旧测试全部通过）

### 4.3 多线程冒烟测试（T2+T3 专项）

```bash
$ gcc -O2 test/t23_thread_smoke.c libint64search.a -o t23_smoke.exe -lpthread
$ ./t23_smoke.exe
T2+T3 multi-thread test PASSED: 200000 find(OK) across 4 threads
```

| 测试参数 | 值 |
|----------|-----|
| 数据规模 | N=100,000 |
| Reader 线程数 | 4 |
| 每个 reader 迭代 | 50,000 find |
| 总 find 调用 | 200,000 |
| 期望全部 OK | ✅ 200,000 / 200,000 |
| destroy 正常返回 | ✅ |

✅ **多 reader 并发 fetch_add(acquire) 正确**
✅ **atomic_load 快照读取一致**
✅ **destroy 等待 reader_count==0 后释放（即使全部 reader 已 join，等待循环空跑不影响正确性）**

> ⚠️ **注意**：此测试不依赖 TSan，**不验证内存竞争**。严格竞态验证在 T6 + V2 (`-fsanitize=thread`) 中实施。

## 5. 实现要点

### 5.1 T2 find() 关键设计

#### 5.1.1 内存序配对

```c
fetch_add(reader_count, +1, acquire)   // 1. 进入临界区
load(path, acquire)                     // 2. 数据快照全部 acquire
load(n, acquire)
load(vals, acquire)
load(dir, acquire)  [if B1]
/* ... 搜索计算 ... */
fetch_sub(reader_count, -1, release)   // 5. 退出临界区
```

**正确性论证**：
- fetch_add(acquire) 阻止本 reader 的后续 load 重排到 fetch_add 之前
- fetch_sub(release) 阻止本 reader 的 fetch_sub 之前的 load 重排到 fetch_sub 之后
- writer 的 exchange(acq_rel) 与 reader 的 fetch_add(acquire) 配对形成同步关系

#### 5.1.2 dir 条件读

```c
const int32_t *d = (p == PATH_B1)
                 ? atomic_load_explicit(&impl->dir, memory_order_acquire)
                 : NULL;
```

**理由**：
- path 是 `_Atomic int`，scalar 路径下不读 dir（性能优化）
- path 先 acquire-load，dir 后 acquire-load：保证看到 path=B1 时 dir 必已就位

#### 5.1.3 bloom 错误路径正确性

```c
if (bf != NULL && !bloom_query(bf, key)) {
    /* ★ 必须先 fetch_sub 再 return,否则 writer 永久等待 */
    atomic_fetch_sub_explicit(&impl->reader_count, 1, memory_order_release);
    if (out_index) *out_index = (size_t)-1;
    return INT64_SEARCH_ERR_NOT_FOUND;
}
```

**关键点**：所有 `return` 路径必须确保 reader_count 已 fetch_sub。
- 设计上: 5 阶段 Step 1 与 Step 5 配对出现
- 例外: bloom 错误路径在 Step 5 之前 return
- 测试已验证: 错误路径与正常路径有相同的 reader_count 进出配对

### 5.2 T3 destroy() 关键设计

#### 5.2.1 等待循环

```c
while (atomic_load_explicit(&impl->reader_count, memory_order_acquire) > 0) {
    platform_thread_yield();
}
```

**关键点**：
- `memory_order_acquire` 保证循环看到的 reader_count==0 之后，reader 在临界区内的所有 load 都已对 destroy 可见
- `platform_thread_yield()` 是 busy-wait：Linux `sched_yield()`，Windows x86 `_mm_pause()`，Windows ARM `Sleep(0)`
- 性能影响：单线程场景下 reader_count==0 立即返回，零开销

#### 5.2.2 释放顺序

```c
/* 1. 释放 bloom */
bloom_destroy(bf);
/* 2. 释放 vals */
platform_aligned_free((void *)v);
/* 3. 释放 dir */
platform_aligned_free((void *)d);
/* 4. 清零 impl + 释放 */
memset(impl, 0, sizeof(*impl));
free(impl);
```

**设计选择**：
- vals/dir/bloom 彼此独立，无引用关系，释放顺序无关
- memset(0) 防御性：防止用户误用悬垂指针（已 destroy 的 handle）
- 实际 Phase 1 release 模式：destructor 只调用一次，无需担心 dtor 内部 rebuild

#### 5.2.3 幂等性保持

```c
if (handle == NULL) return INT64_SEARCH_OK;
```

**保持 Phase 1 行为**：destroy(NULL) 仍返回 OK（不报错）

## 6. 偏差记录

无 [DEBT]。T2+T3 实施严格遵循 DESIGN 文档。

## 7. 关键不变量验证

| 不变量 | 验证 |
|--------|------|
| **INV-1**: reader 永不读到已释放的 vals/dir | T3 destroy 等待 reader_count==0 强制 |
| **INV-3**: 5 字段对 reader 同步可见 | T2 全部 acquire-load |
| **INV-6**: destroy 与 find 并发无 use-after-free | T3 等待 reader + T2 reader 全部 fetch_sub 配对 |
| **INV-8**: _Atomic 字段 lock-free | T1 静态断言 |

## 8. 后续任务依赖解除

- **T4** rebuild() 改造 — 依赖 T1, T2 ✅ 全部完成可执行
- **T5** 头文件警告移除 — 依赖 T1-T4, 等待 T4
- **T6** TSan 测试 — 依赖 T1-T4, 等待 T4
- **T7** L1 COW 测试 — 依赖 T1-T4, 等待 T4
- **V1** Phase 1 回归 — 依赖 T1-T4, 建议 T4 完成后跑

## 9. 关联信息

- **任务规格**: [TASK §3.2, §3.3](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/TASK_task_006_int64_phase2_cow.md)
- **设计参考**:
  - [DESIGN §2.4.1 find() 5 阶段](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/DESIGN_task_006_int64_phase2_cow.md)
  - [DESIGN §2.4.3 destroy()](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/DESIGN_task_006_int64_phase2_cow.md)
  - [DESIGN §4.2 内存序契约](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/DESIGN_task_006_int64_phase2_cow.md)
- **修改文件**:
  - [src/api_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c)
  - [test/t23_thread_smoke.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/t23_thread_smoke.c)
- **参考模板**: [src/api.c:137-176 (Int32 find)](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L137-L176), [src/api.c:288-290 (Int32 destroy)](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L288-L290)
- **前置任务**: [ACCEPTANCE_T1](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/ACCEPTANCE_T1_internal_int64_atomic.md)
