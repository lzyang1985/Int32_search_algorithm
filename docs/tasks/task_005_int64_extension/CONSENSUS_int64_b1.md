---
title: 最终共识文档 — Int64 二期扩展 (Path B1 主线 + Bloom Bypass)
status: Draft
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Architect
task_id: task_005_int64_extension
parent_doc: "docs/tasks/task_005_int64_extension/ALIGNMENT_int64_b1.md"
parent_task: root
source_docs:
  - "docs/requirements/总需求文档.md"
  - "docs/architecture/技术路线.md"
  - "docs/tasks/task_005_int64_extension/ALIGNMENT_int64_b1.md"
  - "docs/meetings/meeting_index/meeting_015_poc_result_review/03_decisions.md"
  - "docs/decisions/poc_int64_report.md"
trace_code:
  - "src/poc_int64_b1.c"
  - "src/poc_int64_b1_crossover.c"
  - "src/poc_bloom_bypass.c"
tags: [consensus, int64, b1, bloom-bypass, phase1]
---

# 最终共识文档 — Int64 二期扩展 (Path B1 主线 + Bloom Bypass)

## 1. 需求描述

### 1.1 功能需求

| 编号 | 功能 | 来源 | 说明 |
|------|------|------|------|
| I64-FR-01 | int64 排序数组构建 | ALIGNMENT §2.1 | 输入 int64_t 数组，qsort 排序 + 单调性校验 |
| I64-FR-02 | high20 目录构建 `build_dir_int64()` | ALIGNMENT §2.1 | build_dir_int64: sign-flip 桶映射 + dir[1048577] int32_t |
| I64-FR-03 | D-110 自动路径选择 `build_decision_int64()` | meeting_015 D-110 | max_bucket > 409 → Scalar Fallback; ≤ 409 → B1 |
| I64-FR-04 | B1 路径查找 `search_int64_b1()` | ALIGNMENT §2.1 | high20 dir 定位 + lo44 4路 cmpeq 扫描 + 每桶回退 |
| I64-FR-05 | Scalar 二分查找 `search_int64_scalar()` | ALIGNMENT §2.1 | int64_t lower_bound，正确性黄金基准 |
| I64-FR-06 | 公开 API 层 | ALIGNMENT §2.1 | create/find/destroy/rebuild/version/set_bloom_bypass/get_bloom_bypass |
| I64-FR-07 | Bloom Bypass 运行时切换 | meeting_013 D-098 | _Atomic(int) bloom_bypass + memory_order_relaxed |
| I64-FR-08 | 版本号 | ALIGNMENT §2.1 | `int64_search_version()` 返回 "libint64search 0.1.0" |

### 1.2 非功能需求

| 编号 | 需求 | 目标值 | 来源 |
|------|------|--------|------|
| I64-NFR-01 | B1 查询性能 | 10M uniform ~318 cy/q (5.30x vs Scalar) | POC §2.2 |
| I64-NFR-02 | 内存占用 | 10M 数据 ≤ 85 MB (vals 80MB + dir 4MB) | ALIGNMENT §5.4 |
| I64-NFR-03 | 正确性验证 | 10000+ 随机查询与 bsearch() 零差异 | POC GATE-1 |
| I64-NFR-04 | ASan/UBSan | 零告警 | 安全规范 |
| I64-NFR-05 | B1 阈值安全 | uniform/skewed 选 B1，zipf 回退 Scalar | POC §2.2 |
| I64-NFR-06 | N > INT32_MAX 防御 | build_dir_int64 入口断言拒绝 | meeting_015 SEC-POC-01 |
| I64-NFR-07 | 构建性能 | 10M 构建 < 5 秒（含排序 + dir + 决策） | 对标 int32 |

---

## 2. 技术实现方案

### 2.1 总体方案

独立库 **libint64search**，以"模式复制"（pattern replication）策略从 int32 库派生，而非代码共享：

- **新增 4 个源文件**：`build_dir_int64.c`、`build_decision_int64.c`、`search_b1_int64.c`、`search_scalar_int64.c`
- **新增 2 个模块文件**：`api_int64.c`、`internal_int64.h`
- **新增 1 个头文件**：`include/int64_search.h`
- **复用** 平台抽象层（`platform_memory`、`platform_cpu`、`platform_thread`）、布隆过滤器（`bloom_filter`）
- **不复用** int32 的查询引擎和构建选路层

### 2.2 路径决策规则

```c
/* build_decision_int64() 决策逻辑 */
int build_decision_int64(const int64_t *vals, size_t n, int32_t *dir) {
    /* Step 1: 一致性校验 */
    if (!dir_validate_int64(dir, n)) return PATH_SCALAR;

    /* Step 2: 扫描最大桶 */
    int32_t max_bucket = 0;
    for (int i = 0; i < INT64_DIR_ENTRIES; i++) {
        int32_t sz = dir[i + 1] - dir[i];
        if (sz > max_bucket) max_bucket = sz;
    }

    /* Step 3: 决策 */
    if (max_bucket > B1_MAX_BUCKET_THRESHOLD_INT64)  /* 409 */
        return PATH_SCALAR;
    return PATH_B1;
}
```

### 2.3 每桶回退策略

```c
/* search_int64_b1() 内：当单个桶过大时桶内回退到二分 */
if (bucket_sz > B1_FALLBACK_THRESHOLD)  /* 409 同阈值 */
    return search_int64_scalar(vals + start, bucket_sz, target);
```

---

## 3. 技术约束

| 约束 | 说明 |
|------|------|
| 语言 | C11（`_Alignas`、`_Generic`、`stdatomic.h`） |
| 编译器 | GCC ≥ 8.0（主力）；MSVC/Clang 后续 |
| SIMD 指令集 | AVX2（4 路 `_mm256_cmpeq_epi64`） |
| 命名空间 | 所有内部符号 `int64_` 前缀 |
| 库独立性 | 独立 .a 文件，符号完全不与 libint32search 冲突 |
| API 风格 | 不透明句柄 `void*`，`int` 错误码 |
| 线程模型 | Phase 1 单线程（COW 为后续阶段） |

---

## 4. 集成方案

### 4.1 平台抽象层共享

```makefile
# 共享 .o 文件，两个库独立链接
libint32search.a: int32_objs + platform_memory.o + platform_cpu.o + bloom_filter.o
libint64search.a: int64_objs + platform_memory.o + platform_cpu.o + bloom_filter.o
```

### 4.2 用户链接方式

```bash
# 仅 int32
gcc -O3 -std=c11 -mavx2 -o myapp myapp.c -L. -lint32search -lm

# 仅 int64
gcc -O3 -std=c11 -mavx2 -o myapp myapp.c -L. -lint64search -lm

# 同时使用
gcc -O3 -std=c11 -mavx2 -o myapp myapp.c -L. -lint32search -lint64search -lm
```

---

## 5. 验收标准

- [ ] `gcc -O3 -std=c11 -mavx2` 编译通过，零警告
- [ ] B1 vs Scalar 交叉验证 10000 次结果逐位一致
- [ ] B1 10M uniform ~318 cy/q（5.30x vs Scalar）
- [ ] ASan/UBSan 零告警
- [ ] B1 阈值正确：uniform 选 B1，zipf 回退 Scalar
- [ ] API 对标 int32 无遗漏（8 个函数）
- [ ] bloom_bypass 5 项验证全部通过
- [ ] sing-flip 内联函数构建和查询共用

---

## 6. 确认状态

| 确认项 | 状态 |
|--------|------|
| 独立库 libint64search | ✅ 已确认 (meeting_012 D-093) |
| Path A 不存在 | ✅ 已确认 (meeting_015 D-109) |
| B1 阈值 409 | ✅ 已确认 (crossover POC 校准) |
| 每桶回退 | ✅ 已确认 (meeting_015 交叉讨论) |
| memory_order relaxed | ✅ 已确认 (meeting_013 D-098) |
| 独立 int64_search_impl_t | ✅ 已确认 (meeting_015 D-110) |

**所有不确定性已解决，可进入 Architect 阶段。**
