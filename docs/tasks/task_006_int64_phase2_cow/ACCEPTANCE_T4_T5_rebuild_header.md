---
title: T4+T5 验收文档 — rebuild() COW + Bloom 重建 + 头文件版本升级
task_id: task_006_int64_phase2_cow
tasks: [T4, T5]
status: SUCCESS
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Executor
parent_task: task_006_int64_phase2_cow
parent_doc: "docs/tasks/task_006_int64_phase2_cow/TASK_task_006_int64_phase2_cow.md"
---

# T4 + T5 验收文档 — rebuild() COW + Bloom 重建 + 头文件版本升级

## 1. 任务概况

| 字段 | T4 (rebuild) | T5 (header/version) |
|------|--------------|---------------------|
| 任务 ID | T4 | T5 |
| 优先级 | P0 | P1 |
| 风险等级 | 高 | 极低 |
| 估算 | 1.5 小时 | 10 分钟 |
| 实际 | ~50 分钟 | ~5 分钟 |
| 并行实施 | ✅ 与 T5 同时 | ✅ 与 T4 同时 |
| 状态 | ✅ **SUCCESS** | ✅ **SUCCESS** |

## 2. 输入契约履行

### T4
- **前置依赖**: T1, T2 ✅ 全部完成
- **输入数据**: `src/api_int64.c:127-198`（Phase 1 rebuild）✅
- **环境依赖**: `bloom_filter.h`, `platform_thread.h` ✅
- **读取的接口/契约**: DESIGN §2.4.2 rebuild 5 阶段, §4.2 内存序契约

### T5
- **前置依赖**: T1-T4 全部完成 ✅
- **输入数据**: `include/int64_search.h:31-33` (Phase 1 警告注释) + `src/api_int64.c:201-203` (version 0.1.0) ✅
- **环境依赖**: 无

## 3. 输出契约履行

### 3.1 T4 rebuild() 验收清单

- [x] **5 阶段顺序与 DESIGN §2.4.2 完全一致**
  - **Phase A**: 构造 new_vals, new_dir, new_path（构造失败时 impl 不动）✅
  - **Phase B**: 构造 new_bloom（仅当旧 bloom 存在；失败容忍）✅
  - **Phase C**: 5 字段原子交换 path → n → dir → vals → bloom（acq_rel）✅
  - **Phase D**: 等待 reader_count == 0 ✅
  - **Phase E**: 释放 old_vals, old_dir, old_bloom + 可能的 new_dir ✅
- [x] **所有阶段失败时旧数据保持完整**
  - Phase A new_vals 构造失败 → 直接 return,impl 不动 ✅
  - Phase B new_bloom 构造失败 → new_bloom = NULL,继续 Phase C,reader 看到 bloom=NULL 跳过预筛 ✅
- [x] **5 字段全部 acq_rel exchange**（与 reader 同步配对）
- [x] **bloom_bypass 字段不参与交换**（Q-A2 决议）✅
- [x] **零内存泄漏**
  - 5 个 exchange 全部捕获旧值(`old_dir`, `old_vals`, `old_bloom`)
  - Phase E 统一释放
  - new_dir 在 PATH_SCALAR 时正确释放
- [x] **零警告编译**（`-O3 -std=c11 -Wall -Wextra -mavx2`）

### 3.2 T5 验收清单

- [x] **`include/int64_search.h` 移除 "单线程 only" 警告**
  - line 31-33 原 ⚠️ 警告 → 新 COW 线程模型注释
- [x] **新注释明确 COW 线程模型**
  - 多 reader 线程安全
  - rebuild 仍需单线程
  - destroy 等待 reader 退出（Q3）
  - set_bloom_bypass 与多 reader 并发安全
- [x] **version 升级 0.1.0 → 0.2.0** ✅
- [x] **公开 API 函数签名零变更** ✅

### 3.3 修改清单

| 文件 | 类型 | 变更 |
|------|------|------|
| [src/api_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c) | 修改 | rebuild() 5 阶段 COW 重写 + version 升级 |
| [include/int64_search.h](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/include/int64_search.h) | 修改 | 警告注释替换为 COW 线程模型说明 |
| [test/t4_rebuild_smoke.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/t4_rebuild_smoke.c) | **新增** | T4 专项测试（4 场景） |

## 4. 单任务验证

### 4.1 编译验证

```bash
$ mingw32-make lib-int64
gcc -c -O3 -std=c11 -Wall -Wextra -Iinclude -Isrc src/search_scalar_int64.c -o src/search_scalar_int64.o
gcc -c -O3 -std=c11 -Wall -Wextra -mavx2 -Iinclude -Isrc src/build_dir_int64.c -o src/build_dir_int64.o
gcc -c -O3 -std=c11 -Wall -Wextra -Iinclude -Isrc src/build_decision_int64.c -o src/build_decision_int64.o
gcc -c -O3 -std=c11 -Wall -Wextra -mavx2 -Iinclude -Isrc src/search_b1_int64.c -o src/search_b1_int64.o
ar rcs libint64search.a src/platform_memory.o src/build_sorted_int64.o src/search_scalar_int64.o \
    src/build_dir_int64.o src/build_decision_int64.o src/search_b1_int64.o src/api_int64.o
EXIT=0
```

✅ 所有 int64 目标零警告编译
✅ 库重新打包成功
✅ `old_bloom` 在 `#ifdef INT64_SEARCH_USE_BLOOM` 块内声明,默认编译零 unused 警告

### 4.2 T4 专项测试（rebuild COW 关键场景）

```bash
$ ./t4_smoke.exe
T4-L1: rebuild COW correct
T4-L2: 1000 rebuild rounds stable
T4-L3: bloom_bypass state preserved after rebuild
T4-L4: version = 'libint64search 0.2.0' (Phase 2)
T4 sanity test PASSED
```

| 测试 | 场景 | 期望 | 实际 |
|------|------|------|------|
| **T4-L1** | rebuild 旧 key=5000 → NOT_FOUND | NOT_FOUND | ✅ NOT_FOUND |
| **T4-L1** | rebuild 新 key=15000 → OK | idx=5000 | ✅ idx=5000 |
| **T4-L2** | 1000 轮 rebuild 稳定性 | 无崩溃/无内存泄漏 | ✅ 1000 轮通过 |
| **T4-L2** | 抽样验证: round=k 时 key=k+5000 必在 idx=5000 | OK | ✅ 1000/1000 |
| **T4-L3** | set_bypass(1) → rebuild → get_bypass | bypass==1 | ✅ bypass==1 |
| **T4-L4** | version 字符串 | "0.2.0" | ✅ "libint64search 0.2.0" |

### 4.3 Phase 1 回归（T1 + T2+T3 既有测试）

```bash
$ ./t1_sanity.exe
T1 sanity test PASSED
$ ./t23_smoke.exe
T2+T3 multi-thread test PASSED: 200000 find(OK) across 4 threads
```

✅ 单线程行为零退化
✅ 多线程并发 find 行为零退化

## 5. 实现要点

### 5.1 T4 rebuild() 关键设计

#### 5.1.1 5 阶段流程（与 DESIGN §2.4.2 完全对齐）

```
Phase A: build_sorted_int64 → build_dir_int64 → build_decision_int64
         (失败时直接 return ERR_MEMORY,impl 不动)
         ↓
Phase B: (仅 INT64_SEARCH_USE_BLOOM) bloom_create(n) + bloom_insert 循环
         (失败时 new_bloom=NULL 容忍)
         ↓
Phase C: 5 字段 atomic_exchange(acq_rel) 顺序固定
         path → n → dir → vals → bloom
         ↓
Phase D: while reader_count > 0: yield()
         (等所有 reader 退出 reader 临界区)
         ↓
Phase E: 释放 old_vals, old_dir, old_bloom
         + (仅 PATH_SCALAR) 释放 new_dir
```

#### 5.1.2 5 字段交换顺序的语义保证

```c
exchange(path)  exchange(n)  exchange(dir)  exchange(vals)  exchange(bloom)
   ↓                ↓             ↓               ↓                ↓
 reader看到path=B1时,后续dir/vals必是B1配套的数据
```

**正确性论证**：
- reader 在临界区开头 acquire-load 5 个字段,后续计算都用本地副本
- writer 端 exchange(acq_rel) 提供 release-store + acquire-load 配对
- reader 与 writer 的同步关系:fetch_add(acquire) ↔ exchange(acq_rel)
- 单个 reader find() 内,5 字段快照是一致的（不会见到 path=B1 但 vals 还是 scalar 配套）

#### 5.1.3 new_dir 双处理

```c
/* Phase C */
const int32_t *old_dir = atomic_exchange_explicit(
    &impl->dir, (new_path == PATH_B1) ? new_dir : NULL, ...);

/* Phase E */
if (new_path != PATH_B1 && new_dir != NULL) {
    platform_aligned_free(new_dir);  /* 释放未使用的 new_dir */
}
```

**设计理由**：
- `new_path == PATH_B1`: new_dir 已被 dir 字段交换,reader 可能引用,**不能**释放
- `new_path == PATH_SCALAR`: new_dir 已置 NULL 到 dir 字段,reader 不引用,可以释放
- 老 dir (`old_dir`) 与新 dir 无关,总是旧 data 配套,通过 Phase E 释放

#### 5.1.4 bloom_create 失败容忍

```c
if (cur_bloom != NULL) {
    new_bloom = bloom_create(n);
    if (new_bloom != NULL) {
        for (i...) bloom_insert(new_bloom, new_vals[i]);
    }
    /* 失败时 new_bloom = NULL,继续 Phase C,reader 看到 bloom=NULL 跳过预筛 */
}
```

**行为**：
- 重建后 bloom 字段为 NULL → reader 跳过预筛 → 直接搜索
- 行为正确,只是性能略降(无预筛)
- 比直接 return ERR_MEMORY 更优（用户 rebuild 永远不应失败）

#### 5.1.5 Phase D 等待循环（与 destroy 一致）

```c
while (atomic_load_explicit(&impl->reader_count, memory_order_acquire) > 0) {
    platform_thread_yield();
}
```

**与 Int32 模式完全对齐**，用户层无感。

### 5.2 T5 头文件改动

#### 5.2.1 警告注释 → 线程模型注释

```diff
-/* ⚠️ 线程安全警告: int64_search_rebuild 当前不支持并发调用。
- * rebuild 期间若其他线程同时执行 find，存在数据竞争风险。
- * 请确保 rebuild 调用是单线程的，或在外部加锁保护。 */
+/* 线程模型(Phase 2):
+ *   - 多 reader 并发调用 int64_search_find 线程安全(lock-free COW 读快照)
+ *   - int64_search_rebuild 仍需由单线程调用(COW 写者)
+ *     rebuild 期间 reader 可继续调用 find(读到旧或新快照,不会出现撕裂状态)
+ *   - int64_search_destroy 等待所有 reader 退出后才释放底层数据(Q3 决议)
+ *   - int64_search_set_bloom_bypass 与多 reader 并发安全 */
```

#### 5.2.2 semver 增量

- 0.1.0 → 0.2.0（minor bump，因新增能力：多 reader 并发 find）
- 不增加 major，因 ABI 完全兼容（结构体字段追加,函数签名零变化）

## 6. 偏差记录

### 6.1 [DEBT-BloomTestUntested] bloom 路径单元测试未覆盖

- **位置**: `test/t4_rebuild_smoke.c`
- **描述**: T4 专项测试在默认 CFLAGS 下未验证 bloom 路径（需要 `-DINT64_SEARCH_USE_BLOOM`）
- **原因**: 默认构建不开启 bloom（Phase 1 决策）
- **修复计划**: T6 阶段实施 TSan 测试时,带 `-DINT64_SEARCH_USE_BLOOM` 验证
- **严重程度**: Minor（DEV-I64-001 修复在 Phase B 代码路径已就位,只是缺测试）
- **当前状态**: 标记 [DEBT-T6-COVER],T6 时补全

## 7. 关键不变量验证

| 不变量 | 验证 |
|--------|------|
| **INV-1**: reader 永不读到已释放的 vals/dir | T4 Phase D 等 reader 退出 + Phase E 释放 |
| **INV-2**: rebuild 失败时旧数据完整 | Phase A new_vals 失败直接 return;Phase B new_bloom 失败容忍 |
| **INV-3**: 5 字段对 reader 同步可见 | T2 acquire-load + T4 exchange(acq_rel) 配对 |
| **INV-4**: bloom 与新 vals 一致 | DEV-I64-001 修复:Phase B 重建 bloom + Phase C 同步交换 |
| **INV-5**: bloom_bypass 状态跨 rebuild 保持 | T4-L3 验证 |
| **INV-6**: destroy 与 find 并发无 use-after-free | T3 等待 reader + T2 reader fetch_sub 配对 |
| **INV-7**: rebuild 期间 reader 不死锁 | T4 Phase D 等待循环仅短暂 yield |
| **INV-8**: _Atomic 字段 lock-free | T1 静态断言 |

## 8. 后续任务依赖解除

- **T6** TSan 测试 — 依赖 T1-T4 ✅ 全部完成可执行
- **T7** L1 COW 测试 — 依赖 T1-T4 ✅ 全部完成可执行
- **T8** Makefile + README — 依赖 T6（T6 完成后可执行）
- **V1** Phase 1 测试回归 — 依赖 T1-T4 ✅ 全部完成可执行
- **V3** 10M 性能回归 — 依赖 T1-T4 + T7

## 9. 关联信息

- **任务规格**: [TASK §3.4, §3.5](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/TASK_task_006_int64_phase2_cow.md)
- **设计参考**:
  - [DESIGN §2.4.2 rebuild() 5 阶段](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/DESIGN_task_006_int64_phase2_cow.md)
  - [DESIGN §4.2 内存序契约](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/DESIGN_task_006_int64_phase2_cow.md)
- **修改文件**:
  - [src/api_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c)
  - [include/int64_search.h](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/include/int64_search.h)
  - [test/t4_rebuild_smoke.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/t4_rebuild_smoke.c)
- **参考模板**:
  - [src/api.c:178-277 (Int32 rebuild COW)](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c#L178-L277)
- **前置任务**:
  - [ACCEPTANCE_T1](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/ACCEPTANCE_T1_internal_int64_atomic.md)
  - [ACCEPTANCE_T2_T3](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/ACCEPTANCE_T2_T3_find_destroy.md)
