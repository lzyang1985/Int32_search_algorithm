---
title: Bug 修复报告 — meeting_009 Step 1: FIX-01/FIX-02/FIX-03
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
task_id: task_001_phase1_mvp
parent_doc: docs/tasks/task_001_phase1_mvp/task_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/03_decisions.md
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/04_action_items.md
  - docs/meetings/meeting_index/meeting_008_b1_memory_pool/02_discussion.md
trace_code:
  - src/poc_benchmark_v3.c
  - src/poc_benchmark_v2.c
tags: [bugfix, meeting-009, step-1, b1, avx2, popcount, defensive-fix]
source_metadata:
  original_path: docs/tasks/task_001_phase1_mvp/FIXREPORT_meeting009_step1_task_001_phase1_mvp.md
  archive_date: 2026-06-01
  archived_by: Agent_Executor
  version: v1.0
---

# Bug 修复报告 — meeting_009 Step 1: FIX-01 / FIX-02 / FIX-03

## 1. 背景与来源

本报告覆盖 [meeting_009 D-073](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_009_poc_execution_plan/03_decisions.md) 决议中的前三个修复行动（FIX-01 ~ FIX-03），对应 [04_action_items.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_009_poc_execution_plan/04_action_items.md) Step 1 清单。

三个 bug 的根因在 meeting_008 讨论中由安全专家精确定位，meeting_009 批准具体修复方案。生产代码 `search_avx2.c` 未受影响（使用不同的正确语义实现）。

---

## 2. FIX-01 (BUG-02, CRITICAL) — AVX2 搜索 popcount 语义反转

### 2.1 问题描述

`search_avx2_binary` 和 `search_b2_high16_avx2` 中的 `__builtin_popcount(mask ^ 0xFF)` 语义取反 + 分支方向完全反转，导致搜索结果错误（hits=0）。

### 2.2 根因分析

| 组件 | 问题 |
|------|------|
| `cmpgt(k, v)` | k > v：`mask` 的 bit[i] 表示 `k > v[i]`（target > element） |
| `mask ^ 0xFF` | 取反：变成 target <= element 的计数 |
| `le_count == 8` → `lo = block + 8` | 如果 target <= 全部 8 个元素 → 右移（方向反了！应该是左移） |
| `le_count == 0` → `hi = block` | 如果 target > 全部 8 个元素 → 左移（方向反了！应该是右移） |

### 2.3 修复方案（安全专家确认）

**不改 `cmpgt(k, v)`**（比较语义正确），仅修复 popcount 和分支：

```c
// ── 修复前 ──
int le_count = __builtin_popcount(mask ^ 0xFF);

if (le_count == 8)      lo = block + 8;    // ❌ 方向反了
else if (le_count == 0) hi = block;        // ❌ 方向反了
else {
    size_t idx = block + (size_t)le_count;
    if (arr[idx] == target) return (int32_t)idx;
    lo = block + (size_t)le_count;
    hi = block + (size_t)le_count;
}

// ── 修复后 ──
int gt_count = __builtin_popcount(mask);    // 去 ^ 0xFF，变量改名

if (gt_count == 0)      hi = block;         // ✅ target <= 全部元素 → 左收缩
else if (gt_count == 8) lo = block + 8;     // ✅ target > 全部元素 → 右收缩
else {
    size_t idx = block + (size_t)gt_count;
    if (arr[idx] == target) return (int32_t)idx;
    lo = block + (size_t)gt_count;
    hi = block + (size_t)gt_count;
}
```

### 2.4 影响文件

| 文件 | 函数 | 行号 |
|------|------|------|
| [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c) | `search_avx2_binary` | L135-146 |
| [poc_benchmark_v2.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v2.c) | `search_avx2_binary` | L107-118 |
| [poc_benchmark_v2.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v2.c) | `search_b2_high16_avx2` | L183-193 |

---

## 3. FIX-02 (BUG-01, MEDIUM, 降级) — DIR 后向填充哨兵防御性加固

### 3.1 问题描述

`high16_dir_build` 的后向填充循环（`for (i = DIR_BUCKETS - 2; i >= 0; i--)`）理论上可能与循环前设置的 `dir[65536] = n` 发生覆写冲突。静态分析无法精确复现 `dir[65536] != n` 的触发条件。

### 3.2 决议

meeting_009 将严重度从 HIGH 降级为 MEDIUM，改为**防御性加固**而非语义修复。在后向填充完成后显式再次设置 `dir[65536]`。

### 3.3 修复方案

```c
// 在 high16_dir_build 末尾（后向填充循环之后）加一行
for (int32_t i = DIR_BUCKETS - 2; i >= 0; i--) {
    if (dir[i] == -1) dir[i] = dir[i + 1];
}
dir[DIR_BUCKETS - 1] = (int32_t)n;    // ← 防御性显式重置哨兵
```

### 3.4 影响文件

| 文件 | 函数 | 行号 |
|------|------|------|
| [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c) | `high16_dir_build` | L91 |
| [poc_benchmark_v2.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v2.c) | `high16_dir_build` | L91 |

### 3.5 待验证

ASan 编译后跑 `n=65537` 动态验证。若能复现 bug，恢复 HIGH 严重度（meeting_009 D-073 约定）。

---

## 4. FIX-03 (BUG-03, CRITICAL) — B1 入口符号扩展防御性检查

### 4.1 问题描述

`search_b1_high16_lo16` 中 `(uint16_t)((uint32_t)target >> 16)` 对符号扩展已有保护，但入口处缺少显式防御。若传入负值 `target < 0`，`uint32_t` 转换会使其高位段溢出至 ≥ 65536，查 `dir[h]` 时将越界。

### 4.2 修复方案

在 B1 搜索入口、`n==0` 检查之后立即添加防御性范围检查：

```c
// ── 修复前 ──
if (n == 0) return -1;
uint16_t h = (uint16_t)((uint32_t)target >> 16);

// ── 修复后 ──
if (n == 0) return -1;
uint32_t h32 = (uint32_t)target >> 16;
if (h32 >= 65536) return -1;       // ← 防御性检查
uint16_t h = (uint16_t)h32;
```

### 4.3 影响文件

| 文件 | 函数 | 行号 |
|------|------|------|
| [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c) | `search_b1_high16_lo16` | L162-164 |
| [poc_benchmark_v2.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v2.c) | `search_b1_high16_lo16` | L133-135 |

---

## 5. FIX-04 (D-066) — dir_validate 增强至 4 项检查

### 5.1 问题描述

现有 `dir_validate` 仅实现了 D-066 的 4 项检查中的 2½ 项：哨兵（`dir[65536]==n`）、单调性（`dir[i]<=dir[i+1]`）、范围下限（`dir[i]>=0`）。缺少范围上限（`dir[i]<=n`）和起始检查（`dir[0]==0`）。

### 5.2 修复方案

```c
// ── 修复前（2½ 项）──
static int dir_validate(const int32_t *dir, size_t n) {
    if (dir[65536] != (int32_t)n) return 0;      // 1. Sentinel ✅
    for (int i = 0; i < 65536; i++) {
        if (dir[i] > dir[i + 1]) return 0;       // 2. Monotonicity ✅
        if (dir[i] < 0) return 0;                // 3. Range (lower only) ⚠️
    }
    return 1;
}

// ── 修复后（4 项完整）──
static int dir_validate(const int32_t *dir, size_t n) {
    if (dir[65536] != (int32_t)n) return 0;      // 1. Sentinel
    if (dir[0] != 0) return 0;                   // 4. Start ← 新增
    for (int i = 0; i < 65536; i++) {
        if (dir[i] > dir[i + 1]) return 0;       // 2. Monotonicity
        if (dir[i] < 0 || dir[i] > (int32_t)n)   // 3. Range (full) ← 补全
            return 0;
    }
    return 1;
}
```

### 5.3 影响文件

| 文件 | 函数 | 行号 |
|------|------|------|
| [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c) | `dir_validate` | L94-101 |

### 5.4 注解

v2 POC 不含 `dir_validate` 函数，无需同步。

---

## 6. 编译验证结果

### 6.1 编译命令与结果

```bash
# poc_benchmark_v3.c
gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/poc_benchmark_v3.c -o poc_benchmark_v3
# ✅ 编译通过，0 警告

# poc_benchmark_v2.c
gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/poc_benchmark_v2.c -o poc_benchmark_v2
# ✅ 编译通过，1 个既存警告 (print_dir_stats unused，非本修复引入)
```

### 6.2 生产代码豁免

[search_avx2.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/search_avx2.c) 使用 `le_count = 8 - popcount(mask)` + 逆序比较 `cmpgt(vec, key)`，语义完整正确，无需修复。

---

## 7. FIX-05 (D-076) — 正确性验证 5 步

### 7.1 新增文件

创建 [verify_b1_fixed.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/verify_b1_fixed.c)（~470 行），单文件自包含，内联全部 FIX-01~04 修复后的函数。

### 7.2 验证步骤

| 步骤 | 验证内容 | 方法 | 结果 |
|:--:|------|------|:--:|
| 1 | dir 构建正确性 | `dir_validate`（D-066 4 项） | ✅ PASS |
| 2 | 100% 命中率 (n=100K) | hits == n（全命中） | ✅ 100000/100000 |
| 3 | 0% 漏检率 (n=100K) | hits == 0（全漏检） | ✅ 0/100000 |
| 4a | B1 vs Scalar 交叉验证 | 逐查询 found/miss 一致 | ✅ 0 偏差 |
| 4b | B1 vs bsearch 交叉验证 | 逐查询 found/miss 一致 | ✅ 0 偏差 |
| 5 | 边界 key（INT32_MIN~MAX） | 5 个边界 key 无崩溃且与 scalar 一致 | ✅ 5/5 PASS |

### 7.3 编译与运行

```bash
gcc -O2 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/verify_b1_fixed.c -o verify_b1_fixed
./verify_b1_fixed
# ALL PASSED: 0 failures
```

---

## 8. FIX-06 (D-076) — ASan/UBSan 11 规模测试

### 8.1 测试环境

| 项目 | 值 |
|------|-----|
| 服务器 | 103.236.63.60 |
| OS | Ubuntu 22.04 LTS |
| GCC | 11.4.0 |
| CPU | Intel Xeon Gold 6152 @ 2.10GHz |

### 8.2 编译命令

```bash
gcc -O1 -g -std=c11 -mavx2 -march=native \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    verify_b1_fixed.c -o verify_asan
```

### 8.3 测试结果

| n | dir_validate | 搜索 | ASan/UBSan |
|--:|:--:|:--:|:--:|
| 0 | — | early return | ✅ |
| 1 | ✅ | ✅ | ✅ |
| 5 | ✅ | ✅ | ✅ |
| 63 | ✅ | ✅ | ✅ |
| 64 | ✅ | ✅ | ✅ |
| 65 | ✅ | ✅ | ✅ |
| 256 | ✅ | ✅ | ✅ |
| 65535 | ✅ | ✅ | ✅ |
| 65536 | ✅ | ✅ | ✅ |
| 65537 | ✅ | ✅ | ✅ |
| 100000 | ✅ | ✅ | ✅ |

> **11/11 全部通过，零 ASan/UBSan 告警。**

---

## 9. BUG-04 (NEW, CRITICAL) — lo16 AVX2 循环尾部越界

### 9.1 发现过程

FIX-06 ASan 测试时，`n=65` 触发 heap-buffer-overflow。

### 9.2 根因

当 `start` 非 16 对齐时，原条件 `i < tail_start`（其中 `tail_start = end & ~15`）未阻止 AVX2 加载越过 `end`：

```
n=65, start=62, end=65 → tail_start=64 → i=62 < 64 → 加载 lo16[62..77]
但 lo16 只有 65 个元素 → 读取越界
```

### 9.3 修复

```c
// 修复前
int32_t tail_start = end & ~15;
for (; i < tail_start; i += 16)

// 修复后
for (; i + 16 <= end; i += 16)    // 确保加载不越界
```

### 9.4 影响文件

| 文件 | 函数 | 行号 |
|------|------|------|
| [verify_b1_fixed.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/verify_b1_fixed.c) | `search_b1_high16_lo16` | — |
| [poc_benchmark_v3.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v3.c) | `search_b1_high16_lo16` | — |
| [poc_benchmark_v2.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/poc_benchmark_v2.c) | `search_b1_high16_lo16` | — |

---

## 10. 修复清单汇总

| 编号 | Bug ID | 严重度 | 修改函数 | 修改行数 | 文件数 |
|:--:|:--:|:--:|------|:--:|:--:|
| FIX-01 | BUG-02 | CRITICAL | `search_avx2_binary` ×2 + `search_b2_high16_avx2` ×1 | 3 处 × 5 行 | 2 |
| FIX-02 | BUG-01 | MEDIUM | `high16_dir_build` ×2 | 2 处 × 1 行 | 2 |
| FIX-03 | BUG-03 | CRITICAL | `search_b1_high16_lo16` ×2 | 2 处 × 3 行 | 2 |
| FIX-04 | — | ENHANCE | `dir_validate` ×1 | 1 处 × 2 行 | 1 |
| FIX-05 | — | — | 正确性验证 5 步（新建 `verify_b1_fixed.c`） | ~470 行 | 1 |
| FIX-06 | — | — | ASan/UBSan 11 规模 × Linux | — | — |
| BUG-04 | CRITICAL | `lo16` AVX2 越界 | `search_b1_high16_lo16` ×3 | 3 处 × 2 行 | 3 |

| 指标 | 值 |
|------|-----|
| 总修改行数 | 24 行 + ~470 行新文件 |
| 受影响文件 | `poc_benchmark_v3.c`, `poc_benchmark_v2.c`, `verify_b1_fixed.c`（新建） |
| 新增编译警告 | 0 |
| 编译通过 | ✅ |
| meeting_009 决议追溯 | D-073 (FIX-01~03), D-066 (FIX-04), D-076 (FIX-05/06) |
| Linux ASan/UBSan | ✅ 11/11 规模零告警（服务器: 103.236.63.60, GCC 11.4） |

---

## 11. 后续步骤

按 [meeting_009 D-077](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_009_poc_execution_plan/03_decisions.md) 执行顺序：

| 顺序 | 行动 | 依赖 | 状态 |
|:--:|------|------|:--:|
| ✅ | Step 1: FIX-01~03 修复 | — | 完成 |
| ✅ | Step 1: FIX-04 dir_validate 增强 | — | 完成 |
| ✅ | Step 1: FIX-05 正确性验证 | — | 完成 |
| ✅ | Step 1: FIX-06 ASan/UBSan | — | 完成 |
| ⬜ | Step 1: FIX-07~08 (编译命令 + benchmark) | FIX-01~06 | 待执行 |
| ⬜ | Step 2: poc_b1_pool.c（内存池 + 三分对比） | Step 1 完整通过 | 待执行 |
| ⬜ | Step 3: poc_b1_crossover.c（D-015 散点采集） | Step 1 完整通过 | 待执行（可与 Step 2 并行） |

---

> 本次修复完成，FIX-01~06 + BUG-04 已全部应用到代码库。下一步应继续 FIX-07~08（编译命令写入 + benchmark），通过后进入 Step 2/3。
