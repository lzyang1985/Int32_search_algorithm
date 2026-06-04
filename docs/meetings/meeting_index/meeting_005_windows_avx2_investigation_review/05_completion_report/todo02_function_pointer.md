---
title: TODO-02 完成记录 — 构建时函数指针方案
status: Frozen
created_at: 2026-05-29
updated_at: 2026-05-30
author: Agent_Executor
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
task_ref: TODO-02
resolution: D-038
tags: [build-time, function-pointer, internal.h, api.c]
---

# TODO-02 构建时函数指针方案

## 修改的文件

### `src/internal.h`

```c
#define INT32_SEARCH_AVX2_MIN_N 10000000ULL

typedef struct {
    int32_t  *vals;
    size_t    n;
    int       path;
    int32_t   (*search_fn)(const int32_t *vals, size_t n, int32_t key, size_t *out_index);
    uint8_t   avx2_capable;
} int32_search_impl_t;
```

### `src/api.c` — `create()` 末尾

```c
impl->avx2_capable = (uint8_t)platform_cpu_has_avx2();
if (impl->avx2_capable && impl->n > INT32_SEARCH_AVX2_MIN_N) {
    impl->search_fn = search_avx2_find;
} else {
    impl->search_fn = search_scalar_find;
}
```

### `src/api.c` — `find()` 热路径

**修改前**（每次查询两次分支 + CPU 检测调用）→ **修改后**（单行间接调用）：

```c
return impl->search_fn(impl->vals, impl->n, key, out_index);
```

## 设计要点

| 要点 | 说明 |
|------|------|
| 分派时机 | `create()` 时一次性指派，之后不可变 |
| 函数签名 | 与 `search_avx2_find` / `search_scalar_find` 一致，不传 `impl` |
| Phase 2 兼容 | B1 路径就绪后仅改 `create()` 中的 `search_fn` 赋值 |
| 阈值 | `INT32_SEARCH_AVX2_MIN_N = 10,000,000` |

## 验证

| 检查项 | 结果 |
|--------|------|
| 编译（6 .o + 静态库） | ✅ 零错误零警告 |
| 单元测试 9 项 | ✅ 全部通过 |
| 正确性测试 50 万次查询 | ✅ 0 mismatches |
