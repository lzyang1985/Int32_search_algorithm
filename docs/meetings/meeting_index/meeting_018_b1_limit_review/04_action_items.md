---
title: B1路径极限评审会 — 行动项
meeting_id: meeting_018_b1_limit_review
status: Reviewing
created_at: 2026-06-08
updated_at: 2026-06-08
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_018_b1_limit_review/meeting_README.md
parent_task: root
---

# B1路径极限评审会 — 行动项

## 统计

| 优先级 | 总数 | 新增 | 来自 meeting_017 |
|--------|------|------|-----------------|
| P0（安全立即） | 1 | 1 | 0 |
| P1（本周） | 8 | 4 | 4 |
| P2（条件/延后） | 3 | 1 | 2 |
| **总计** | **12** | **6** | **6** |

---

## P0 — 安全立即

| 编号 | 行动项 | 负责 | 决议 | 工作量 | 说明 |
|------|--------|------|------|--------|------|
| **ACT-41** | Int64 B1 移植 D-143 等效防御 | Sec | D-148 | 30min | `search_b1_int64.c` 增加 start<0/end<0/(size_t)end>n 检查 |

---

## P1 — 本周推进

| 编号 | 行动项 | 负责 | 决议 | 工作量 | 说明 |
|------|--------|------|------|--------|------|
| ACT-42 | PGO+LTO+64B 对齐 | Backend | D-130 | 2-3天 | MinGW instrumentation-based PGO + LTO；注意训练 workload ≠ benchmark workload |
| ACT-43 | Huge Pages POC | Algo | D-131 | 1-2h | Linux `madvise(MADV_HUGEPAGE)`；`perf stat -e dTLB-load-misses` 验证 |
| ACT-44 | 软件预取距离调优 | Algo | D-132 | 1-2天 | `__builtin_prefetch` distance/locality 参数实验；callgrind 验证 |
| ACT-45 | Int64 B1 D-142 小桶标量路径（阈值=4） | Algo | D-149 | 30min | 2 行改动；`if (bucket_sz < 4)` 跳过 SIMD 设置 |
| ACT-46 | Dir fuzz 基础覆盖率 | Sec | D-151 | 1天 | 注入 start/end 值对到 B1 函数；Int32+Int64 双覆盖 |
| **ACT-47** | `find_with_hint` 最小原型 | Fullstack | D-150 | 2天 | B1 路径 hint 优先；实验性头文件 |
| **ACT-48** | 编译器 flag 实验 | Backend | — | 1h | `-march=skylake -falign-loops=32` 基准对比 |
| ACT-49 | 热键缓存 workload 埋点 | Algo | D-133a | 2天 | `#ifdef INT32_SEARCH_PROFILE_KEYS`；top-1%命中率判定 |

---

## P2 — 条件/延后

| 编号 | 行动项 | 负责 | 决议 | 触发条件 | 说明 |
|------|--------|------|------|----------|------|
| ACT-50 | D-140 重验证（PGO + -fno-unroll-loops） | Backend | D-147 | D-130 PGO 完成后 | bench_100 对比：PGO基线 vs PGO+D-140+-fno-unroll-loops |
| ACT-51 | 热键缓存完整实现 | Algo | D-133b | D-133a 验证 top-1% > 40% | 256-1024 项 direct-mapped cache |
| ACT-52 | DX 收尾（bench分位数、CMakeLists、Rust RAII示例） | Fullstack | — | 维护模式准入前 | bench_100.ps1 增加 P50/P90/P99 |

---

## 已完成行动项（本次会议相关）

| 编号 | 行动项 | 决议 | 状态 |
|------|--------|------|------|
| — | meeting_018 会议文档初始化 | — | ✅ |

---

## 依赖关系

```
ACT-41 (Int64 D-143)  ← 独立，立即执行
ACT-42 (PGO+LTO)      ← 前置依赖: 构建系统准备
ACT-43 (Huge Pages)    ← 需要 Linux 环境
ACT-44 (预取)          ← 建议在 ACT-42 后执行（代码布局已稳定）
ACT-45 (Int64 D-142)   ← 独立，可与 ACT-41 同时
ACT-46 (Dir fuzz)      ← 前置依赖: ACT-41 完成后才能覆盖 Int64
ACT-47 (find_with_hint) ← 独立，不阻塞其他
ACT-48 (编译器flag)    ← 独立，1h 快速实验
ACT-49 (热键埋点)      ← 独立
---
ACT-50 (D-140 重验证)  ← 触发条件: ACT-42 完成
ACT-51 (热键缓存)      ← 触发条件: ACT-49 验证通过
ACT-52 (DX 收尾)       ← 维护模式准入前
```
