---
title: 最终共识文档 — Phase 1.5 多线程 COW (Path A)
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Architect
task_id: task_002_phase15_cow
parent_doc: "docs/tasks/task_002_phase15_cow/ALIGNMENT_task_002_phase15_cow.md"
parent_task: task_001_phase1_mvp
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/tasks/task_002_phase15_cow/ALIGNMENT_task_002_phase15_cow.md"
trace_code:
  - "src/api.c"
  - "src/internal.h"
  - "include/int32_search.h"
tags: [consensus, phase15, cow, multithreading]
---

# 最终共识文档 — Phase 1.5 多线程 COW (Path A)

## 1. 需求描述

### 1.1 功能需求

| 编号 | 功能 | 来源 | 说明 |
|------|------|------|------|
| FR-04 | 数据重建 `int32_search_rebuild()` | 总需求文档 FR-04 | COW 方式替换数据集，不阻塞并发查询 |
| FR-07 | 并发安全 `find()` | 技术路线 §5 | 多线程只读查询安全，与 rebuild 并发正确 |
| FR-08 | COW 原子指针交换 | 技术路线 §5.1 | `_Atomic int32_t*` + release/acquire 语义 |
| FR-09 | 旧版本延迟回收 | 总需求文档 §6.2 | 读写计数 + 自旋等待，零泄漏 |
| FR-10 | 安全销毁 `destroy()` | Phase 1 增强 | 等待所有 reader 退出后释放 |

### 1.2 非功能需求

| 编号 | 需求 | 目标值 |
|------|------|--------|
| NFR-05 | 并发正确性 | ThreadSanitizer 零告警 |
| NFR-06 | 查询路径零锁 | `find()` 热路径无互斥锁，仅原子计数 |
| NFR-07 | rebuild 不阻塞查询 | `find()` 在 `rebuild()` 执行期间继续返回正确结果 |
| NFR-08 | 内存安全 | `-fsanitize=address` 零告警；无 use-after-free |
| NFR-09 | 单线程性能无损 | Phase 1 现有 benchmark 数据不退化（< 1% 差异） |

---

## 2. 技术实现方案

### 2.1 总体方案

在 Phase 1 MVP 代码基础上，以最小侵入方式添加 COW 多线程支持：

- **新增** `platform_thread.c/h`：C11 `stdatomic.h` 原子操作封装
- **修改** `internal.h`：`vals` → `_Atomic int32_t*`，`n` → `_Atomic size_t`，新增 `reader_count`
- **修改** `api.c`：新增 `rebuild()`，改造 `find()`/`destroy()` 并发安全
- **新增** `test/test_thread.c`：并发压力测试

### 2.2 并发模型（最终确认）

```
Writer (rebuild)                        Reader (find)
─────────────────                      ───────────────
build_sort_and_validate(data, n)
  → new_vals
                                        atomic_fetch_add(&reader_count, 1, acquire)
atomic_store(&n, new_n, release)        
atomic_store(&vals, new_vals, release)  
                                        v = atomic_load(&vals, acquire)
                                        n = atomic_load(&n, acquire)
                                        search_fn(v, n, key, out_index)
自旋等待 reader_count == 0              
                                        atomic_fetch_sub(&reader_count, 1, release)
platform_aligned_free(old_vals)
```

### 2.3 关键设计决策（Q1-Q4 决议）

| 决策 | 决议 | 理由 |
|------|------|------|
| Q1 自旋 vs 让步 | **自旋等待** | 临界区 ~50ns，让步系统调用开销更大 |
| Q2 `n` 原子性 | **`_Atomic size_t`** | 一行改动消除越界风险，lock-free 零开销 |
| Q3 rebuild 重评估引擎 | **不重新评估** | 换数据不改变硬件能力 |
| Q4 rebuild/destroy 互斥 | **调用方保证** | Phase 1 已有约束，不额外加锁 |

### 2.4 技术约束

| 约束 | 说明 |
|------|------|
| C 标准 | C11（`stdatomic.h`、`_Atomic`、`memory_order_*`） |
| 编译器 | GCC ≥ 8.0（主力），MSVC 通过 CMake 支持 |
| 线程库 | 仅标准 C11 `<stdatomic.h>`，不依赖 `<pthread.h>` |
| 修改范围 | 仅 api.c / internal.h / int32_search.h，查询引擎零修改 |
| 查询引擎 | `search_avx2_find` / `search_scalar_find` 代码不变 |

---

## 3. 集成方案

### 3.1 与 Phase 1 代码的关系

```
Phase 1 文件                         Phase 1.5 变更
────────────                         ──────────────
include/int32_search.h              + int32_search_rebuild() 声明
src/internal.h                      vals→_Atomic, n→_Atomic, +reader_count
src/api.c                           +rebuild(), find()/destroy() 改造
src/platform_thread.h               [新增]
src/platform_thread.c               [新增]
src/search_avx2.c                   无变更
src/search_scalar.c                 无变更
src/build_sorted.c                  无变更
src/platform_memory.c               无变更
src/platform_cpu.c                  无变更
test/test_unit.c                    应全部继续通过
test/test_correctness.c             应全部继续通过
test/test_boundary.c                应全部继续通过
test/test_thread.c                  [新增] 并发测试
benchmark/bench_main.c              无变更
Makefile                            +platform_thread.o 规则
README.txt                          更新编译命令
```

### 3.2 对 Phase 2 的前向兼容

Phase 2 B1 COW 需要的基座：
- ✅ `_Atomic` 指针交换模式已建立（Path A 单指针）
- ✅ `reader_count` 读写计数机制就绪
- ✅ `platform_thread.h` 原子操作封装可供复用
- ✅ B1 COW 仅需扩展为三指针 struct 级原子交换（D-017）

---

## 4. 验收标准

### 4.1 编译验收

- [ ] `gcc -O3 -std=c11 -mavx2` 编译通过，零警告
- [ ] `Makefile` 三目标（`lib`/`test`/`bench`）全部通过
- [ ] `-fsanitize=address,undefined` 编译零告警
- [ ] `-fsanitize=thread` 编译通过（ThreadSanitizer 可用）

### 4.2 功能验收

- [ ] `int32_search_rebuild()` 替换数据后 `find()` 返回新数据结果
- [ ] `rebuild` 失败时旧数据不受影响，`find()` 仍返回旧数据结果
- [ ] `rebuild(NULL, data, n)` 返回 `ERR_NULL_HANDLE`
- [ ] `rebuild(handle, NULL, n)` 返回 `ERR_INVALID_ARG`
- [ ] `rebuild(handle, data, 0)` 返回 `ERR_INVALID_ARG`

### 4.3 并发验收（ThreadSanitizer）

- [ ] 1 reader + 1 writer 并发：TSan 零告警
- [ ] N readers + 1 writer 并发：TSan 零告警（N ≥ 4）
- [ ] `destroy` 等待所有 reader 退出：TSan 零告警
- [ ] `rebuild` 期间 `find` 持续返回正确结果（旧数据或新数据）

### 4.4 回归验收

- [ ] Phase 1 单元测试 9/9 继续 PASS
- [ ] Phase 1 边界测试 54/54 继续 PASS
- [ ] Phase 1 正确性交叉验证 500K 查询零差异
- [ ] Phase 1 benchmark 10M 数据性能不退化（< 1%）

### 4.5 内存安全验收

- [ ] ASan 编译测试零告警
- [ ] Valgrind `--tool=memcheck` 无泄漏
- [ ] `rebuild` 循环 100 次后内存无增长（旧版本正确释放）

---

## 5. 风险与缓解

| 风险 | 等级 | 缓解措施 |
|------|------|----------|
| 原子操作在 `find()` 热路径增加开销 | 低 | `_Atomic size_t` lock-free，仅 2 条原子指令 |
| 自旋等待在 reader 慢速时耗 CPU | 低 | `rebuild` 低频操作，reader ~50ns 临界区 |
| TSan 与 ASan 不能同时使用 | 中 | 分两次编译验证：ASan（单线程正确性）+ TSan（并发正确性） |
| `_Atomic` 在旧 GCC 的 bug | 低 | GCC ≥ 8.0 已验证 `_Atomic` 稳定 |

---

## 6. 关联信息

- 父文档：[ALIGNMENT_task_002_phase15_cow.md](ALIGNMENT_task_002_phase15_cow.md)
- 后续文档：DESIGN_task_002_phase15_cow.md
- 前置任务：[task_001_phase1_mvp](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_001_phase1_mvp/task_README.md)
