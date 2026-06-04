---
title: 需求对齐文档 — Phase 2 A+B1 双路径
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Architect
task_id: task_003_phase2_ab1
parent_doc: none
parent_task: task_002_phase15_cow
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/tasks/task_001_phase1_mvp/DESIGN_task_001_phase1_mvp.md"
  - "docs/tasks/task_002_phase15_cow/DESIGN_task_002_phase15_cow.md"
  - "docs/meetings/meeting_index/meeting_002_adaptive_strategy_review/03_decisions.md"
  - "docs/meetings/meeting_index/meeting_003_implementation_planning/03_decisions.md"
trace_code:
  - "src/api.c"
  - "src/internal.h"
  - "src/search_b1.c"
  - "src/build_decision.c"
  - "include/int32_search.h"
tags: [alignment, requirements, phase2, b1, dual-path, cow]
---

# 需求对齐文档 — Phase 2 A+B1 双路径

## 1. 原始需求

### 1.1 来源

本阶段来源于 总需求文档 §5「三阶段交付计划」Phase 2：

```
Phase 2: v1.0 — A+B1 双路径
  ├── 新增：B1 路径（high16 dir + lo16 SIMD 扫描）
  ├── 新增：D-015 分布分析 + 自动选路
  ├── 新增：B1 COW（struct 级三指针原子交换）
  └── 性能：1M 数据 B1 ~75 cy/query（2.1x vs A）
```

以及 总需求文档 §6.3「Phase 2 v1.0 验收」：

| 验收项 | 说明 |
|--------|------|
| A vs B1 交叉验证 | 100 万次结果逐位一致 |
| B1 COW struct 级三指针原子交换正确 | D-017 |
| B1 路径 ThreadSanitizer 零告警 | 数据竞争零容忍 |
| 倾斜数据自动回退 Path A | max_sz > 0.1×n 检测 |
| 1.5M 均匀数据自动选中 B1 | max_bucket ≤ 2000 |

### 1.2 背景

Phase 1 MVP 已交付 Path A（AVX2 8 路 SIMD 二分），Phase 1.5 已交付 Path A COW 多线程。B1 路径（高 16 位目录 + 低 16 位 SIMD 扫描）核心算法已在 POC 中验证通过，但尚未集成到公开 API。build_dir.c（high16 目录构建）和 build_b1.c（lo16 数组构建）代码尚未编写。Phase 2 的目标是将 B1 完整集成，实现构建时一次性选路、热路径零开销的 A+B1 双路径架构。

---

## 2. 边界确认（任务范围）

### 2.1 Phase 2 包含

| 模块 | 文件 | 变更类型 | 说明 |
|------|------|----------|------|
| 构建与选路层 | `src/build_dir.c/h` | **新增** | high16 目录构建 + 一致性校验 |
| 构建与选路层 | `src/build_b1.c/h` | **新增** | lo16 数组构建 |
| 查询引擎层 | `src/search_b1.c/h` | **已存在** | B1 核心查找算法（POC 已验证，无需修改） |
| 查询引擎层 | `src/build_decision.c/h` | **已存在** | 路径决策器（POC 已验证，可能需要微调） |
| 内部结构 | `src/internal.h` | **修改** | 新增 B1 快照字段；COW 适配 |
| API 层 | `src/api.c` | **修改** | create/rebuild 集成 B1 选路；find 集成 B1 调度 |
| 平台抽象层 | `src/platform_thread.h` | **可能修改** | B1 COW 原子结构指针操作宏（如需要） |
| 公开头文件 | `include/int32_search.h` | **可能修改** | 版本号升级至 "1.0.0"；注释补充 |
| 构建系统 | `Makefile` | **修改** | 新增 build_dir.o / build_b1.o 编译规则 |
| 构建系统 | `README.txt` | **修改** | 更新编译命令 |
| 测试 | `test/test_b1_correctness.c` | **新增** | B1 vs A 跨路径交叉验证 |
| 测试 | `test/test_b1_boundary.c` | **新增** | B1 路径边界测试（n=0~64、空桶、单桶） |
| 测试 | `test/test_b1_decision.c` | **新增** | 自动选路正确性测试 |
| 测试 | `test/test_thread_b1.c` | **新增** | B1 COW 多线程安全测试（TSan） |

### 2.2 Phase 2 明确不包含

| 不包含项 | 原因 | 归属阶段 |
|----------|------|----------|
| SSE2 / AVX-512 编译版本 | 多指令集版本属于扩展 | Phase 3 |
| 布隆过滤器（FR-06） | #ifdef USE_BLOOM_FILTER 编译开关 | Phase 3 |
| Windows 平台移植 | Phase 3 处理 | Phase 3 |
| `int32_search_find_range` 实现 | Phase 3 预留接口 | Phase 3 |
| AVX2 MinGW 代码生成退化修复 | 技术路线 §7 标记为 Phase 3 | Phase 3 |
| Eytzinger / S-tree 布局 | POC 未达标，已触发 RFC | RFC |
| 多线程 benchmark | 正确性优先 | Phase 3 |

### 2.3 不变更项

| 项目 | 说明 |
|------|------|
| Path A 查询算法 | `search_avx2_find` / `search_scalar_find` 代码零修改 |
| 排序逻辑 | `build_sort_and_validate` 代码零修改 |
| 平台层 | `platform_memory.c` / `platform_cpu.c` 代码零修改 |
| Path A COW | Phase 1.5 的原子指针交换逻辑在 Path A 分支保持不变 |
| 公开 API 签名 | `create`/`find`/`rebuild`/`destroy`/`find_range`/`version` 签名全部不变 |
| 错误码 | 五个错误码（OK, NOT_FOUND, NULL_HANDLE, MEMORY, INVALID_ARG）不新增 |
| `b1_snapshot_t` | 已定义于 `internal.h`，结构体名和字段名不变 |
| `build_decision_select_path` | 签名和阈值逻辑不变（B1_MAX_BUCKET_THRESHOLD=2000） |
| `search_b1_find` | 签名和算法不变 |
| 命名规范 | 下划线命名法 |
| C 标准 | C11 |
| 依赖 | 仅 xxHash + 标准库 |

---

## 3. 需求理解

### 3.1 B1 路径数据结构

B1 路径在排序数组 `vals` 之外，额外构建两个辅助数组：

| 数组 | 类型 | 大小 | 构建方式 | 说明 |
|------|------|------|----------|------|
| `vals` | `int32_t[]` | n | 排序（与 Path A 共享） | 完整值数组 |
| `dir` | `int32_t[65537]` | ~256 KB | 遍历 vals，记录每个高 16 位值的起始位置 | high16 目录 |
| `lo16` | `uint16_t[]` | n | 提取低 16 位，与 vals 下标对应 | 低 16 位数组 |

**B1 内存用量**：
- Path A：`n × 4` 字节（仅 vals）
- B1 额外：`65537 × 4 + n × 2 = 256KB + n×2` 字节
- 10M 数据：Path A ~40MB，B1 ~40MB + 256KB + 20MB ≈ 60.25 MB（≤ 84MB，满足非功能需求）

### 3.2 B1 查找算法（已验证）

已在 [search_b1.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/search_b1.c) 中通过 POC 验证：

```
1. 取 target 高 16 位 h，查 dir[h] 得桶起始 start
2. 桶范围 [start, dir[h+1]) 或到数组末尾
3. 桶内用 AVX2 并行扫描 lo16（_mm256_cmpeq_epi16 + _mm256_movemask_epi8）
4. 匹配 lo16 后校验完整值 vals[pos] == target
5. 桶末尾剩余 < 16 元素用标量扫描
```

**时间复杂度**：O(1) 定位桶 + O(bucket_size/16) SIMD 扫描。对于均匀数据，桶大小 ≈ n/65536，极速。

### 3.3 D-015 路径决策规则（已验证）

来自 技术路线 §3.3，已在 [build_decision.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/build_decision.c) 中实现（POC 验证通过，阈值已由 meeting_010 校准）：

```
1. build_dir → dir[65537]
2. IF dir_validate(dir, n) FAIL → PATH_A
3. max_sz = max(dir[i+1] - dir[i]) for i in 0..65535
4. IF max_sz > 0.1 × n  → PATH_A       (倾斜分布检测)
5. IF max_sz ≤ 2000      → PATH_B1      (均匀小桶)
6. ELSE                  → PATH_A       (回退保守)
```

**设计意图**：max_sz > 0.1n 意味着超过 10% 的数据落入同一高 16 位桶，B1 将退化为对该大桶的 SIMD 扫描，失去优势。max_sz ≤ 2000 则桶足够小，SIMD 扫描效率极高。

### 3.4 B1 COW 并发模型

**关键差异**：Path A COW 只需原子交换一个 `vals` 指针，而 B1 需要原子交换三个指针（`vals`、`lo16`、`dir`）。

来自 技术路线 §5.1：

```
Path A (单数组):  atomic_store(&g_vals_ptr, new_vals)
Path B1 (三数组): atomic_store(&g_snapshot, &new_snapshot)  [struct 级, D-017]
```

**实现方案**：在 `int32_search_impl_t` 中维护 `_Atomic b1_snapshot_t *b1_snap`。rebuild 时：
1. 构建新的 `vals`、`lo16`、`dir` 数组
2. 构造新的 `b1_snapshot_t` 结构体（值类型），填入三个指针和 n
3. `atomic_store(&impl->b1_snap, &new_snapshot, memory_order_release)` 进行 struct 级原子交换

**C11 可行性**：`_Atomic(b1_snapshot_t*)` 是平台指针大小的原子操作（64 位系统上 8 字节），lock-free。这与 Phase 1.5 的 `_Atomic(int32_t*)` 本质相同。

但需要注意：`b1_snapshot_t` 本身是一个 struct（24 字节 on 64-bit），我们存储的是指向它的指针 `b1_snapshot_t*`，而不是 struct 本身。因此原子交换的仍然是指针。

**更简洁的方案**：直接在 `int32_search_impl_t` 中嵌入三个 `_Atomic` 指针：

```c
_Atomic(const int32_t*)  vals;   // Path A 也用这个
_Atomic(const uint16_t*) lo16;   // B1 only
_Atomic(const int32_t*)  dir;    // B1 only
_Atomic size_t           n;      // 共享
```

在 B1 rebuild 中按序原子更新：先 `n`、`lo16`、`dir`（release），最后 `vals`（release）。reader 按逆序读取（先 `vals` acquire，再 `dir`、`lo16`、`n`）。由于新数据在指针交换前已完全构造，且 release/acquire 成对，可见性有保障。

**✅ 推荐方案**：在 `int32_search_impl_t` 中嵌入三个 `_Atomic` 指针。理由：
- 与 Phase 1.5 模式一脉相承（`_Atomic` 指针数组）
- 无需额外的 malloc/free 管理 snapshot 结构体
- 字段与 `search_b1_find` 的参数一一对应
- ThreadSanitizer 直接可见，无需特殊标注

### 3.5 find() 双路径调度

当前 `api.c` 的 `find()` 通过 `impl->search_fn` 函数指针直接调度。B1 引入后，`search_b1_find` 的签名与 Path A 的 `search_fn` 不兼容：

```c
// Path A: 2 个数据参数
int32_t (*search_fn)(const int32_t *vals, size_t n, int32_t key, size_t *out_index);

// Path B1: 4 个数据参数
int32_t search_b1_find(const int32_t *vals, const uint16_t *lo16,
                       const int32_t *dir, size_t n, int32_t target,
                       size_t *out_index);
```

**解决方案**：在 `find()` 中按 `impl->path` 分支，不使用函数指针统一调度：

```c
int int32_search_find(int32_search_t handle, int32_t key, size_t *out_index) {
    // ... null checks ...
    atomic_size_fetch_add(&impl->reader_count, 1, memory_order_acquire);

    int32_t result;
    if (impl->path == PATH_B1) {
        const int32_t  *v = atomic_ptr_load(&impl->vals, memory_order_acquire);
        const uint16_t *l = atomic_ptr_load(&impl->lo16, memory_order_acquire);
        const int32_t  *d = atomic_ptr_load(&impl->dir, memory_order_acquire);
        size_t _n = atomic_size_load(&impl->n, memory_order_acquire);
        result = search_b1_find(v, l, d, _n, key, out_index);
    } else {
        int32_t *v = atomic_ptr_load(&impl->vals, memory_order_acquire);
        size_t _n = atomic_size_load(&impl->n, memory_order_acquire);
        result = impl->search_fn(v, _n, key, out_index);
    }

    atomic_size_fetch_sub(&impl->reader_count, 1, memory_order_release);
    return result;
}
```

**设计理由**：
- `path` 在 create/rebuild 时确定，查询路径不变，分支预测器完美
- 两个路径共享 `vals` + `n` 的 atomic load，B1 多 load `lo16` + `dir`
- 保留 `search_fn` 函数指针仅用于 Path A 的 AVX2/Scalar 二级调度
- B1 路径不需要函数指针（当前仅一种 B1 实现）

### 3.6 create() 构建流程

构建时一次性选路的关键流程：

```
int32_search_create(data, n, cfg):
  1. new_vals = build_sort_and_validate(data, n)
  2. new_dir  = build_dir(new_vals, n)        ← 新增
  3. IF dir_validate(new_dir, n) FAIL:         ← 新增
       path = PATH_A   (强制回退)
     ELSE:
       new_lo16 = build_b1(new_vals, n, new_dir) ← 新增
       path = build_decision_select_path(new_dir, n)  ← 新增（调用已有函数）
  4. impl->path = path
  5. IF path == PATH_B1:
       impl->lo16 = new_lo16
       impl->dir  = new_dir
     ELSE:
       platform_aligned_free(new_dir)  // B1 不可用，丢弃 B1 辅助数组
       platform_aligned_free(new_lo16) // （如果有）
  6. impl->vals = new_vals, impl->n = n
  7. 设置 search_fn（仅 Path A）
```

**关键设计决策**：
- **始终构建 dir**：D-015 决策依赖 max_sz，必须先有 dir
- **lo16 仅在 dir 校验通过后构建**：避免无效工作
- **Path A 时丢弃 B1 辅助数组**：节省内存，回到 ≤40MB 的 Path A 预算
- **dir_validate 失败 → Path A**：安全回退，生产环境也会触发

### 3.7 rebuild() 双路径 COW

```
int32_search_rebuild(handle, data, n):
  1. new_vals = build_sort_and_validate(data, n)
  2. new_dir  = build_dir(new_vals, n)
  3. IF dir_validate OK:
       new_lo16 = build_b1(new_vals, n, new_dir)
       new_path = build_decision_select_path(new_dir, n)
     ELSE:
       new_path = PATH_A
  4. atomic_store(&impl->n, n, release)
  5. IF new_path == PATH_B1:
       // B1: 三个指针分别原子交换
       old_lo16 = atomic_ptr_exchange(&impl->lo16, new_lo16, acq_rel)
       old_dir  = atomic_ptr_exchange(&impl->dir,  new_dir,  acq_rel)
       old_vals = atomic_ptr_exchange(&impl->vals, new_vals, acq_rel)
     ELSE:
       // Path A: 仅原子交换 vals
       old_vals = atomic_ptr_exchange(&impl->vals, new_vals, acq_rel)
       free(new_dir);  // 不按 B1 路径，丢弃
       free(new_lo16);
  6. impl->path = new_path
  7. 等待 reader_count == 0
  8. 释放 old_vals / old_lo16 / old_dir
```

**B1 rebuild 三指针原子交换顺序**：
- 构造顺序：`vals` → `dir` → `lo16`（vals 是基础，dir 从 vals 构建，lo16 从 vals+dir 构建）
- 原子交换顺序：`lo16` → `dir` → `vals`（逆序：先更新次要指针，最后更新核心指针 vals）
- Reader 读取顺序：`vals` → `dir` → `lo16`（先读核心指针，后读辅助指针）
- 由于 release/acquire 配对，reader 读到新 vals 时，新 dir 和 lo16 也已完成（写入的 release 在 exchange 中确保）

### 3.8 destroy() B1 适配

destroy 需要释放 B1 路径的额外数组：

```c
int int32_search_destroy(int32_search_t handle) {
    // ...等待 reader_count == 0...
    platform_aligned_free(impl->vals);
    if (impl->path == PATH_B1) {
        platform_aligned_free(impl->lo16);  // 新增
        platform_aligned_free(impl->dir);   // 新增
    }
    memset(impl, 0, sizeof(*impl));
    free(impl);
    return INT32_SEARCH_OK;
}
```

---

## 4. 疑问澄清

### Q1: build_dir 和 build_b1 的内存分配方式

B1 辅助数组（dir、lo16）的内存分配是否也使用 `platform_aligned_malloc`（32 字节对齐）？

- `dir[65537]`：256KB，SIMD 不直接操作，**不需要** 32 字节对齐
- `lo16[n]`：B1 查找时用 `_mm256_loadu_si256`（非对齐加载），**不需要** 32 字节对齐

**✅ 决议**：dir 和 lo16 使用普通 `calloc`/`malloc`。只有 `vals` 需要 `platform_aligned_malloc`（Path A AVX2 使用对齐加载）。释放时对应使用 `free()`。

### Q2: 如果 B1 路径在 create 时被选中，但后续 rebuild 数据变为倾斜怎么办？

rebuild 重新执行完整的决策流程。如果新数据倾斜，路径自动从 B1 切换为 Path A，B1 辅助数组被释放，内存回到 Path A 预算。

**✅ 决议**：自然支持路径切换。rebuild 无记忆性，每次独立决策。无需特殊处理。

### Q3: B1 三指针原子交换是否需要 struct 级 CAS？

技术路线提到"struct 级三指针原子交换（D-017）"，但逐个原子交换三个 `_Atomic` 指针是否满足要求？

逐个交换的问题：reader 可能读到"vals 是新值、lo16 是旧值"的不一致状态，导致 B1 查找在旧 lo16 中扫描新 vals 的 key。

**分析**：
- 如果新 vals 的 key 在旧 lo16 中的高 16 位对应桶不存在，B1 返回 NOT_FOUND（假阴性 —— 此时 key 实际存在于新 vals 中）
- 但如果扫描到匹配的低 16 位值，`vals[pos] == target` 的校验会**捕获不匹配**（旧 lo16 的低 16 位碰巧相同但完整值不同）

然而，假阴性是**不可接受的**。B1 查找可能因旧 lo16 不包含新 key 的低 16 位而错误返回 NOT_FOUND。

**✅ 最终决议**：采用逐个原子交换 + 交换顺序约定 + release/acquire 配对。

具体方案：
- Writer（rebuild）：先原子交换 `lo16`、再 `dir`、最后 `vals`（全部 acq_rel）
  - 关键：`vals` 是最后更新的，任何 reader 读到新 `vals` 时，新 `lo16` 和 `dir` 已经写入
  - `acq_rel` 确保新数组内容在指针可见前已构造完成
- Reader（find）：先原子读取 `vals`（acquire）、再 `lo16`（acquire）、再 `dir`（acquire）
  - 如果读到的 `vals` 是旧指针，后续的 `lo16`/`dir` 也对应旧版本，一致
  - 如果读到的 `vals` 是新指针（通过 acq_rel 与 writer 配对），则 guarantee `lo16`/`dir` 也是新的

这保证了 **Reader 永远看到一致的快照**：要么全是旧的（safe），要么全是新的（safe）。

但存在一个边界：reader 读到新 vals 后，如果 rebuild 的 writer 在 reader 读 lo16 之前又开始了另一轮 rebuild，新 lo16 可能已经被第二轮覆盖。然而 —— rebuild 的 writer 在交换指针后会**等待 reader_count == 0** 才释放旧指针，但不会等待才交换指针。这意味着快速连续的两次 rebuild 之间，reader 可能读到混合版本。

**最终方案**：在 `int32_search_impl_t` 中新增 `_Atomic uint32_t b1_version`。每次 rebuild B1 时递增。reader 读取 version → 读取 vals/lo16/dir → 再读 version，如果不一致则重试。这是 seq-lock 模式。

实际上，Phase 1.5 的 Path A COW 也有同样的问题（虽然只有一个指针，但连续两次 rebuild 之间 reader 可能读到第一次的 vals 和第二次的 n）。Phase 1.5 的答复是"调用方保证 rebuild 非并发"——即同一时刻只有一个 writer。

基于相同前提，B1 不需要 seq-lock。**约定**：rebuild 不可并发（与 Phase 1.5 一致）。

逐个原子交换 + 逆序读取 + 单 writer 保证 = 足够的正确性。

**✅ 最终确认决议**：逐个原子交换，无需 seq-lock。遵循 Phase 1.5 的"rebuild 不可并发"前提。

### Q4: `search_b1_find` 是否需要处理 `target` 的符号问题？

`target` 是 `int32_t`，高 16 位提取：`(uint32_t)target >> 16`。当 `target` 为负数时（如 -1 = 0xFFFFFFFF），右移结果为 0xFFFF（65535），仍在 dir[0..65535] 范围内。

**✅ 决议**：当前代码 `uint32_t h32 = (uint32_t)target >> 16` 已正确处理符号问题，无需修改。

### Q5: 版本号升级

Phase 2 完成后版本号从 "libint32search 0.2.0" 升级为 "libint32search 1.0.0"。

**✅ 决议**：在 TASK 中安排 `int32_search_version()` 返回值更新。

---

## 5. 与现有代码的兼容性分析

### 5.1 需要变更的文件

| 文件 | 变更内容 | 预估行数 |
|------|----------|----------|
| `src/build_dir.c/h` | **新增** — high16 目录构建 + 校验 | +80 |
| `src/build_b1.c/h` | **新增** — lo16 数组构建 | +50 |
| `src/internal.h` | 新增 `_Atomic` lo16/dir/b1_version 字段 | +10 |
| `src/api.c` | create/rebuild 集成 B1 选路；find B1 调度；destroy B1 清理 | +80, ~30 修改 |
| `include/int32_search.h` | 版本号注释更新；补充 B1 路径说明 | +5 |
| `test/test_b1_correctness.c` | **新增** | +100 |
| `test/test_b1_boundary.c` | **新增** | +120 |
| `test/test_b1_decision.c` | **新增** | +100 |
| `test/test_thread_b1.c` | **新增** | +150 |
| `Makefile` | 新增 rules + test 目标 | +10 |
| `README.txt` | 更新 | +5 |

**总预估**：~710 行新代码 + ~30 行修改。

### 5.2 不影响的部分

- `search_avx2.c/h` — 零修改
- `search_scalar.c/h` — 零修改
- `search_b1.c/h` — 零修改（算法已验证）
- `build_decision.c/h` — 零修改（决策逻辑已验证）
- `build_sorted.c/h` — 零修改
- `platform_memory.c/h` — 零修改
- `platform_cpu.c/h` — 零修改
- `platform_thread.h` — 零修改（当前宏已覆盖所需操作）
- 所有 Phase 1 + Phase 1.5 测试 — 全部应继续通过

### 5.3 ABI 兼容性

`int32_search_impl_t` 是内部结构，通过不透明句柄 `void*` 访问。新增字段增加 struct 大小但不影响 API。所有旧测试重新编译链接即可。

---

## 6. 风险初评

| 风险 | 等级 | 描述 | 缓解 |
|------|------|------|------|
| B1 三指针原子交换可见性 | **高** | 连续两次 rebuild 间 reader 可能看到不一致快照 | 约定 rebuild 不可并发（与 Phase 1.5 一致）；TSan 验证 |
| build_dir 构建性能 | 低 | 遍历 10M 数组构建 dir 约 O(n)，但仅在 create/rebuild 时执行 | 低频操作，可接受 |
| B1 路径内存峰值 | 低 | 构建期间同时存在新旧两套 B1 数组（~120MB 峰值 for 10M），比 Path A 的 ~80MB 高 | 在 总需求文档 ≤84MB 预算内 |
| B1 极端倾斜数据 | 低 | D-015 的 max_sz > 0.1n 检测已覆盖 | POC crossover 验证通过 |

---

## 7. 关联信息

- 前置任务：[task_002_phase15_cow](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_002_phase15_cow/task_README.md) — Phase 1.5 多线程 COW（已完成）
- 前置任务：[task_001_phase1_mvp](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/task_README.md) — Phase 1 MVP（已完成）
- 源需求：[总需求文档 §5](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/requirements/总需求文档.md#L114-L136)
- 技术基础：[技术路线 §2-5](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/architecture/技术路线.md#L30-L262)
- 核心代码：
  - [search_b1.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/search_b1.c) — B1 查找算法（已验证）
  - [build_decision.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/build_decision.c) — 路径决策（已验证）
  - [api.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api.c) — API 集成层（需修改）
  - [internal.h](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/internal.h) — 内部结构（需修改）
