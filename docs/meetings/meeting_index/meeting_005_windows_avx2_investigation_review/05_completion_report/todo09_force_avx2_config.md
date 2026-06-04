---
title: TODO-09 完成记录 — INT32SEARCH_FORCE_AVX2 编译配置
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
task_ref: TODO-09
resolution: D-039, SEC-03
tags: [ci, force-avx2, api.c, Makefile, SEC-03]
---

# TODO-09 INT32SEARCH_FORCE_AVX2 编译配置

## 修改文件

### `src/api.c` — 新增 `#ifdef INT32SEARCH_FORCE_AVX2` 分支

```c
#ifdef INT32SEARCH_FORCE_AVX2
    if (impl->avx2_capable) {
        impl->search_fn = search_avx2_find;
    } else {
        impl->search_fn = search_scalar_find;
    }
#else
    if (impl->avx2_capable && impl->n > INT32_SEARCH_AVX2_MIN_N) {
        impl->search_fn = search_avx2_find;
    } else {
        impl->search_fn = search_scalar_find;
    }
#endif
```

定义 `INT32SEARCH_FORCE_AVX2` 时绕过 `INT32_SEARCH_AVX2_MIN_N` 阈值，强制启用 AVX2（仍检查 CPU 支持）。

### `Makefile` — 新增 `test-force-avx2` 目标

以 `-DINT32SEARCH_FORCE_AVX2` 编译全部测试，在小数据集上跑 AVX2 路径。

## 服务器验证（103.236.63.60, GCC 11.4.0）

- 编译通过（`-mavx2` + `-fsanitize=address,undefined`）
- `int32search_correctness_force_avx2`：5 组测试 PASS，0 mismatches（n=100~n=100000，均走 AVX2）
- `int32search_boundary_force_avx2`：18/18 PASS

**缓解 SEC-03**：AVX2 代码腐烂风险 — 现在 CI 可强制小数据集跑 AVX2 路径。
