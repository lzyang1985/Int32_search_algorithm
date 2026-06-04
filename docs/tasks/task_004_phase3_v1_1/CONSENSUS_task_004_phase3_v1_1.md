---
title: CONSENSUS — Phase 3 v1.1 扩展与跨平台
status: Draft
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Architect
task_id: task_004_phase3_v1_1
parent_task: root
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/meetings/meeting_index/meeting_011_phase2_audit_review/03_decisions.md"
  - "docs/tasks/task_004_phase3_v1_1/ALIGNMENT_task_004_phase3_v1_1.md"
tags: [phase3, v1.1, consensus]
---

# CONSENSUS — Phase 3 v1.1 扩展与跨平台

## 1. 最终需求描述

实现 Int32 查找算法库 **v1.1 版本**，在已有 A+B1 双路径基础上新增以下能力：

| 功能 | 描述 | 验收标准 |
|------|------|----------|
| **范围查询** `find_range()` | 给定 [low, high]，返回首个下标 + 区间内元素个数 | 与标量 lower_bound+upper_bound 100 万次结果逐位一致 |
| **布隆过滤器** | 编译开关 `USE_BLOOM_FILTER`，1% 假阳性率，k=3 hash | 假阳性率 ≤ 1%（10M 数据实测），不命中查询跳过后端 |
| **Windows `platform_thread_yield()`** | `_mm_pause()` + `Sleep(0)` 双策略 | COW 旧版本回收不忙循环 |
| **meeting_011 并行项** | ACT-03 ~ ACT-10（P2/P3 文档+安全加固+清理） | 逐项完成 |

---

## 2. 技术实现方案

### 2.1 find_range() 实现方案

```
设计决策：B1 路径回退到 vals[] 标量二分（用户确认）

入参校验 → 获取 impl 快照（COW acquire）
   ↓
若 impl->path == PATH_B1 → 直接用 impl->vals[] 做 lower_bound + upper_bound
若 impl->path == PATH_A  → 同上（vals[] 本身就是有序的）
   ↓
out_first = lower_bound(vals, n, low)     // 首个 >= low
out_last  = upper_bound(vals, n, high)    // 首个 > high
out_count = out_last - out_first
   ↓
若 out_count == 0 → ERR_NOT_FOUND
否则 → OK
```

- **复杂度**：O(log n) × 2（两次独立二分），不依赖 B1 内部结构
- **并发**：使用 COW acquire 快照，与 rebuild 不冲突
- **B1 lo16 不可用原因**：lo16 仅存储低 16 位且无序，无法支持有序范围遍历

### 2.2 布隆过滤器实现方案

```
设计参数（用户确认）：
  假阳性率 p = 0.01 (1%)
  bits/element m/n = -ln(p) / (ln 2)² ≈ 9.57
  取 m/n = 10（安全余量）
  Hash 数量 k = (m/n) × ln 2 ≈ 6.93 → 取 k = 3（保守，降低计算开销）

  10M 数据 = 10M × 10 bits = 12.5 MB 位数组

数据结构（新增 bloom_filter.h）：
  typedef struct {
      uint8_t *bits;       // 位数组
      size_t   m;          // 位数组长度（bits）
      uint32_t seeds[3];   // k=3 个 xxHash seed
  } bloom_filter_t;
```

**集成点**：

| 阶段 | 行为 |
|------|------|
| `create()` | 若 `cfg->use_bloom` → 分配 bloom + 遍历 data 批量插入；失败时回滚 |
| `find()` | 若 `impl->bloom` 非空 → 先查 bloom；`NOT_PRESENT` 直接返回 `ERR_NOT_FOUND` |
| `destroy()` | 销毁 bloom 位数组 |
| `rebuild()` | 新数据 → 新 bloom 构建，随 COW 原子交换 |

**编译开关**：`#ifdef INT32_SEARCH_USE_BLOOM`（用户通过 `-D` 启用）

**`int32_search_config_t` 修改**：`reserved[8]` → `use_bloom`（第一个 int）+ `reserved[7]`

### 2.3 Windows 移植方案

```
platform_thread_yield() 实现（用户确认 _mm_pause + Sleep(0)）：

#ifdef _WIN32
  #include <windows.h>
  #if defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h>
    #define platform_thread_yield() do { _mm_pause(); } while(0)
  #else
    #define platform_thread_yield() Sleep(0)
  #endif
#else
  #include <sched.h>
  #define platform_thread_yield() sched_yield()
#endif
```

- 仅改 `platform_thread.h`，其他文件无变更
- 不新增源文件
- Windows 线程原子操作继续使用 C11 `stdatomic.h`（MinGW 支持）

---

## 3. 技术约束

| 约束 | 说明 |
|------|------|
| C 标准 | C11（`stdatomic.h`、`_Alignas`） |
| 编译器 | GCC ≥ 8.0（主力），MinGW-W64（Windows） |
| 不引入 | SSE2 编译版本、AVX-512 编译版本（D-087 P3，Phase 3 不实现） |
| 不引入 | MSVC 支持（后续评估） |
| 不修复 | MinGW AVX2 性能退化（已知风险，不阻塞） |
| 保持 | 不透明句柄 + 错误码 API 风格 |
| 保持 | 下划线命名法 |

---

## 4. 集成方案

### 4.1 新增文件

| 文件 | 说明 |
|------|------|
| `src/bloom_filter.h` | 布隆过滤器数据结构和接口声明 |
| `src/bloom_filter.c` | 布隆过滤器实现（create/insert/query/destroy） |
| `test/test_range.c` | find_range 正确性测试 |
| `test/test_bloom.c` | 布隆过滤器假阳性率测试 |

### 4.2 修改文件

| 文件 | 变更内容 |
|------|----------|
| `include/int32_search.h` | `config_t.reserved[8]` → `int use_bloom; int reserved[7]`；find_range 注释从 stub 改为已实现 |
| `src/internal.h` | 新增 `_Atomic` bloom 字段（COW 支持） |
| `src/api.c` | find_range stub → 完整实现；create/destroy/rebuild 中集成 bloom |
| `src/platform_thread.h` | `platform_thread_yield()` 空实现 → `_mm_pause`/`Sleep(0)`/`sched_yield` |
| `README.txt` | 新增 find_range + bloom 编译命令 |
| `Makefile` | 新增 bloom 源文件编译 + 测试目标 |
| `CMakeLists.txt` | 同步 Makefile 变更 |

### 4.3 meeting_011 并行项

| ACT | 文件 | 变更 |
|-----|------|------|
| ACT-03 | `src/platform_thread.h` | 与 §2.3 Windows 移植合并实现 |
| ACT-04 | `docs/tasks/task_003_phase2_ab1/ACCEPTANCE_task_003_phase2_ab1.md` | 补充偏差 A/B/C |
| ACT-05 | `README.txt`、`include/int32_search.h` | 注释补充 |
| ACT-06 | `docs/tasks/task_003_phase2_ab1/TODO_task_003_phase2_ab1.md` | 优先级更新 |
| ACT-07 | `docs/tasks/task_003_phase2_ab1/FINAL_task_003_phase2_ab1.md` | 标注 rc |
| ACT-08 | `src/build_b1.c` | `n > SIZE_MAX / sizeof(uint16_t)` 检查 |
| ACT-09 | `src/build_dir.c` | `n > (size_t)INT32_MAX` 检查 |
| ACT-10 | `src/internal.h` | 移除 `b1_snapshot_t.weighted_avg` |

---

## 5. 验收标准

### 5.1 find_range

- [ ] `gcc -O3 -std=c11 -mavx2` 编译通过，零警告
- [ ] Path A 100 万次随机 `find_range` 结果与 `bsearch()` lower_bound+upper_bound 完全一致
- [ ] Path B1 100 万次随机 `find_range` 结果与 `bsearch()` 完全一致（含边界：空区间、全区间、单元素）
- [ ] n=0~64 所有值 `find_range` 测试通过（含空数组、单元素数组）
- [ ] `-fsanitize=address,undefined` 零告警
- [ ] ThreadSanitizer：并发 `find_range` + `rebuild` 零告警

### 5.2 布隆过滤器

- [ ] `gcc -O3 -std=c11 -DINT32_SEARCH_USE_BLOOM` 编译通过
- [ ] 不启用时零开销（`#ifdef` 条件编译，代码路径不受影响）
- [ ] 启用后 10M 数据假阳性率 ≤ 1%
- [ ] find() 不命中时 bloom 正确拦截（不命中 key 不进入后端查找）
- [ ] find() 命中时 bloom 不误拒（bloom 阳性 → 后端验证，最终结果正确）
- [ ] `-fsanitize=address,undefined` 零告警

### 5.3 Windows 移植

- [ ] MinGW-W64 下 `platform_thread_yield()` 编译通过（含 `-mavx2`）
- [ ] `_mm_pause()` 用于 x86/x64，`Sleep(0)` 用于其他
- [ ] COW 重建并发压力测试 ThreadSanitizer 零告警（Linux 验证）

### 5.4 meeting_011 并行项

- [ ] ACT-03 ~ ACT-10 逐项完成（参见 §4.3）

---

## 6. 确认清单

| # | 确认项 | 状态 |
|---|--------|------|
| 1 | 布隆假阳性率 1%，k=3 hash | ✅ 确认 |
| 2 | B1 路径 find_range 回退 vals[] 二分 | ✅ 确认 |
| 3 | Windows yield `_mm_pause` + `Sleep(0)` | ✅ 确认 |
| 4 | SSE2/AVX-512 本次不实现（P3 搁置） | ✅ 确认（D-087） |
| 5 | MinGW AVX2 退化不阻塞交付 | ✅（技术路线 §7） |
| 6 | MSVC 不在此次范围 | ✅ 确认 |
| 7 | meeting_011 P2/P3 并行推进 | ✅ 确认（D-086） |

---

## 7. 关联信息

- ALIGNMENT 文档：[ALIGNMENT_task_004_phase3_v1_1.md](ALIGNMENT_task_004_phase3_v1_1.md)
- 总需求文档：[总需求文档.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/requirements/总需求文档.md)
- 技术路线文档：[技术路线.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/architecture/技术路线.md)
- Phase 2 审计决议：[03_decisions.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_011_phase2_audit_review/03_decisions.md)
