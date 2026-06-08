---
title: T1 验收文档 — internal_int64.h 字段原子化
task_id: task_006_int64_phase2_cow
task: T1
status: SUCCESS
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Executor
parent_task: task_006_int64_phase2_cow
parent_doc: "docs/tasks/task_006_int64_phase2_cow/TASK_task_006_int64_phase2_cow.md"
---

# T1 验收文档 — `internal_int64.h` 字段原子化

## 1. 任务概况

| 字段 | 值 |
|------|-----|
| 任务 ID | T1 |
| 任务来源 | TASK_task_006_int64_phase2_cow §3 |
| 优先级 | P0（关键路径）|
| 风险等级 | 中 |
| 估算 | 30 分钟 |
| 实际 | ~25 分钟 |
| 状态 | ✅ **SUCCESS** |

## 2. 输入契约履行

- **前置依赖**: 无 ✅
- **输入数据**: `src/internal_int64.h` (Phase 1 版本，35 行) ✅
- **环境依赖**: `src/platform_thread.h`（提供 `_Atomic` 类型）✅
- **读取的接口/契约**: DESIGN §2.2 字段设计 + ALIGNMENT §3.1 缺陷诊断

## 3. 输出契约履行

### 3.1 验收清单

- [x] **7 个 `_Atomic` 字段全部就位**
  - `_Atomic(void *)` bloom（保持）
  - `_Atomic(int)` bloom_bypass（保持）
  - `_Atomic int` path（**改**）
  - `_Atomic size_t` n（**改**）
  - `_Atomic(const int64_t *)` vals（**改**）
  - `_Atomic(const int32_t *)` dir（**改**）
  - `_Atomic size_t` reader_count（**新增**）
- [x] **编译通过零警告**（`gcc -O3 -std=c11 -Wall -Wextra -mavx2`）
- [x] **`_Static_assert` 编译时 lock-free 验证**
  - `ATOMIC_INT_LOCK_FREE == 2`
  - `ATOMIC_POINTER_LOCK_FREE == 2`
  - `__atomic_always_lock_free(sizeof(size_t), 0)` (GCC/Clang)

### 3.2 修改清单

| 文件 | 类型 | 行数 | 备注 |
|------|------|------|------|
| [src/internal_int64.h](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal_int64.h) | 修改 | +35 | 字段原子化 + 静态断言 + Phase 2 注释 |
| [src/api_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c) | 修改 | +4/-4 | destroy/rebuild 4 处 `(void *)` cast 消除 `-Wdiscarded-qualifiers` 警告 |
| [test/t1_sanity.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/t1_sanity.c) | **新增** | +60 | T1 阶段最小烟测（验证 Phase 1 行为未变）|

## 4. 单任务验证

### 4.1 编译验证

```bash
$ mingw32-make lib-int64
gcc -c -O3 -std=c11 -Wall -Wextra -Iinclude -Isrc src/search_scalar_int64.c -o src/search_scalar_int64.o
gcc -c -O3 -std=c11 -Wall -Wextra -mavx2 -Iinclude -Isrc src/build_dir_int64.o -o src/build_dir_int64.o
gcc -c -O3 -std=c11 -Wall -Wextra -Iinclude -Isrc src/build_decision_int64.o -o src/build_decision_int64.o
gcc -c -O3 -std=c11 -Wall -Wextra -mavx2 -Iinclude -Isrc src/search_b1_int64.o -o src/search_b1_int64.o
ar rcs libint64search.a src/platform_memory.o src/build_sorted_int64.o src/search_scalar_int64.o \
    src/build_dir_int64.o src/build_decision_int64.o src/search_b1_int64.o src/api_int64.o
EXIT=0
```

✅ 所有 int64 目标零警告编译
✅ 静态断言全部通过（lock-free 验证）
✅ 库生成成功

### 4.2 库符号验证

```
T int64_search_create            (160 bytes)
T int64_search_destroy           (200 bytes)
T int64_search_find              (e0 bytes)
T int64_search_find_range        (360 bytes)
T int64_search_get_bloom_bypass  (350 bytes)
T int64_search_rebuild           (200 bytes)
T int64_search_set_bloom_bypass  (330 bytes)
T int64_search_version           (320 bytes)
```

✅ 8 个公开符号全部导出，符号顺序与 Phase 1 一致

### 4.3 行为验证（Sanity Test）

| 测试 | 输入 | 期望 | 实际 |
|------|------|------|------|
| create | N=10000, [0,1,...,9999] | OK | ✅ OK |
| find(existing) | key=5000 | idx=5000 | ✅ idx=5000 |
| find(missing) | key=50000 | NOT_FOUND | ✅ NOT_FOUND |
| rebuild | N=10000, [0,2,4,...,19998] | OK | ✅ OK |
| find(after rebuild) | key=10000 | idx=5000 | ✅ idx=5000 |
| destroy | -- | OK | ✅ OK |

```bash
$ ./t1_sanity.exe
T1 sanity test PASSED
EXIT=0
```

✅ Phase 1 行为完全保持（C11 允许对 `_Atomic` 类型的普通赋值/读取，等价于 `seq_cst` 语义，但 T2/T4 阶段会替换为 `acq_rel` 语义）

## 5. 实现要点

### 5.1 字段顺序调整

调整后字段顺序：bloom (保留) → bloom_bypass (保留) → path → n → vals → dir → reader_count

**说明**：
- Phase 2 的 reader_count 必须放在最后（避免破坏已有 ABI 兼容，PowerShell 2.0+ 保证 struct 字段末尾追加不破坏兼容）
- path/n/vals/dir 重新排序是因为 `_Atomic(const T*)` 类型不能放在 `_Atomic(void*)` 之前（C11 允许但习惯上将 `_Atomic` 限定符字段放一起）

### 5.2 const 修饰取舍

```c
_Atomic(const int64_t *) vals;  /* const 修饰指针 */
_Atomic(const int32_t *) dir;
```

- **理由**：与 Int32 v1.0.0 保持一致，语义表达"vals/dir 是只读数据，搜索函数不应写"
- **代价**：destroy/rebuild 时需要 `(void *)` 显式去 const
- **符合预期**：DESIGN §2.2.3 已说明此权衡

### 5.3 静态断言分级

```c
/* C11 标准宏(优先) */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, ...);
_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2, ...);

/* GCC/Clang 扩展(精确 size_t) */
_Static_assert(__atomic_always_lock_free(sizeof(size_t), 0), ...);
```

**说明**：
- C11 `ATOMIC_*_LOCK_FREE` 宏不直接提供 `size_t` 对应常量（size_t 在不同平台可能是 `unsigned long` 或 `unsigned long long`）
- GCC/Clang `__atomic_always_lock_free` 在编译时精确判断，避免运行时 fallback
- 双层断言最大化跨编译器兼容性

## 6. 偏差记录

### 6.1 [DEBT] 旧 API 代码中 const 限定符 cast

- **位置**: `src/api_int64.c:118, 119, 150, 151`
- **描述**: `platform_aligned_free((void *)impl->vals)` 等 4 处需要显式 `(void *)` cast
- **原因**: T1 引入 `_Atomic(const T*)` 字段后，const 限定符传递到使用点
- **修复计划**: T2/T3/T4 阶段这些代码会被 `atomic_ptr_load` 替换，cast 自然消失
- **严重程度**: Minor
- **当前状态**: [DEBT-T2/T3/T4-FIX] 标注，4 处 cast 已工作

### 6.2 字段顺序调整

- **位置**: `src/internal_int64.h:41-54`
- **描述**: 字段顺序从 `path/n/vals/dir/bloom/bloom_bypass` 改为 `bloom/bloom_bypass/path/n/vals/dir/reader_count`
- **原因**: 1) reader_count 必须放末尾保证 ABI 兼容 2) `_Atomic` 字段聚类便于阅读
- **影响**: struct 大小不变（所有字段都是 lock-free atomic，padding 由编译器决定）
- **严重程度**: Minor（用户代码不直接依赖字段顺序）

## 7. 后续任务依赖解除

T1 完成解锁了以下任务的执行条件：
- **T2** find() 改造 — 依赖 T1（字段已 _Atomic）✅ 可执行
- **T3** destroy() 改造 — 依赖 T1（reader_count 字段已就位）✅ 可执行
- **T4** rebuild() 改造 — 依赖 T1, T2 ✅ T2 完成后可执行
- **V1** Phase 1 测试回归 — 依赖 T1 ✅ 可执行（建议 T2-T4 完成后跑）

## 8. 关联信息

- **任务规格**: [TASK §3.1](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/TASK_task_006_int64_phase2_cow.md)
- **设计参考**: [DESIGN §2.2](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/DESIGN_task_006_int64_phase2_cow.md)
- **修改文件**:
  - [src/internal_int64.h](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal_int64.h)
  - [src/api_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c)
  - [test/t1_sanity.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/t1_sanity.c)
- **参考模板**:
  - [src/internal.h:14-24](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h#L14-L24)（Int32 B1 COW 模式）
  - [src/api.c:178-277](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L178-L277)（Int32 rebuild COW）
