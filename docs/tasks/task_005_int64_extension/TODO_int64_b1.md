---
title: 待办事项 — Int64 二期扩展 Phase 1
status: Draft
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Auditor
task_id: task_005_int64_extension
parent_doc: "docs/tasks/task_005_int64_extension/FINAL_int64_b1.md"
parent_task: root
tags: [todo, int64, b1, phase1, phase2]
---

# 待办事项 — Int64 二期扩展 Phase 1

> 以下 TODO 可直接回流给开发阶段（Execute），按类型和优先级排序。

---

## 🔧 修复 (Fix)

| # | 描述 | 严重度 | 文件 |
|---|------|--------|------|
| TODO-01 | 消除 `test_int64.c:47` unused variable `cfg` 警告 — 添加 `(void)cfg` | Minor | `test/test_int64.c` |

---

## ⚡ 优化 (Optimization)

| # | 描述 | 严重度 | 说明 |
|---|------|--------|------|
| TODO-02 | `build_sorted_int64` 增加 `n > SIZE_MAX / sizeof(int64_t)` 溢出检查 | Minor | 当前 n 受限于 INT32_MAX 故不触发，防御性编程 |
| TODO-03 | `_mm256_set1_epi64x(target)` 可提升到循环外（每次迭代重新 broadcast 浪费 ~3 cy） | Minor | `search_b1_int64.c:26` |

---

## ⚙️ 配置 (Config)

| # | 描述 | 严重度 | 说明 |
|---|------|--------|------|
| TODO-04 | Makefile 增加 `lib-int64` / `test-int64` / `clean-int64` 目标 | Medium | 目前仅 README.txt 手写命令 |
| TODO-05 | `.gitignore` 排除 int64 POC 和 test 可执行文件 | Low | 保持仓库清洁 |

---

## 🧪 测试 (Test)

| # | 描述 | 严重度 | 说明 |
|---|------|--------|------|
| TODO-06 | 增加 10M uniform 性能回归测试 | Medium | 当前仅 POC 有 benchmark 数据 |
| TODO-07 | 增加 zipf α=1.0 退化场景测试（验证阈值 409 正确触发回退） | Medium | 当前 L3 仅验证 uniform → B1 |
| TODO-08 | 增加 `search_int64_b1` 的 SIMD mask==0 快速路径测试（纯标量对比） | Low | 防御性覆盖 |
| TODO-09 | 增加 rebuild 后 bloom_bypass 状态保持测试 | Low | validate 设计文档 "rebuild 保留 bloom_bypass" |

---

## 🏗️ Phase 2 前置条件

| # | 描述 | 类型 | 说明 |
|---|------|------|------|
| TODO-10 | 设计 `int64_b1_snapshot_t` COW 原子交换方案 | 设计 | vals+dir 共 2 指针 + size_t = 24 字节，x86-64 超出 16 字节 lock-free 上限，需 spinlock 或引用计数 |
| TODO-11 | bloom 重建逻辑：rebuild() 中根据 cfg.use_bloom 重建 bloom | 实现 | 当前仅 destroy 旧 bloom |
| TODO-12 | `int32_t dir` → `int64_t dir` 迁移方案评估（n > 2^31 场景） | 设计 | 4MB → 8MB |

---

## 📋 Phase 3 远期

| # | 描述 | 类型 |
|---|------|------|
| TODO-13 | `find_range` 在 B1 路径下的实现策略 | 设计 |
| TODO-14 | Eytzinger 布局 POC（标量二分优化） | POC |
| TODO-15 | AVX-512 8 路 int64 二分探索 | POC |

---

## 📊 统计

| 类型 | 数量 |
|------|------|
| 修复 | 1 |
| 优化 | 2 |
| 配置 | 2 |
| 测试 | 4 |
| Phase 2 前置 | 3 |
| Phase 3 远期 | 3 |
| **总计** | **15** |

---

**本次审计结束，无更多自动处理。**
