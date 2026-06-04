---
title: 优化方向讨论会 — 行动项
meeting_id: meeting_016_optimization_direction
status: Reviewing
created_at: 2026-06-02
updated_at: 2026-06-03
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_016_optimization_direction/meeting_README.md
parent_task: root
---

# 优化方向讨论会 — 行动项

## 优先级说明

| 级别 | 含义 | 时限 |
|------|------|------|
| P0 | 本次会议后立即执行 | 今日 |
| P1 | 本周内 | 7 天 |
| P2 | 有条件/有资源时 | 不限 |

---

## P0 行动项（今日）

| # | 行动项 | 类型 | 工作量 | 来源决议 | 状态 |
|---|--------|------|--------|----------|------|
| ACT-27 | 修复 TODO-01: `test_int64.c:47` 添加 `(void)cfg` | 修复 | 5 分钟 | D-115 | ✅ 完成 |
| ACT-28 | 修复 TODO-02: `build_sorted_int64` 入口添加 `n > SIZE_MAX / sizeof(int64_t)` 溢出检查 | 优化 | 5 分钟 | D-115 | ✅ 完成 |
| ACT-29 | 修复 TODO-03: `search_b1_int64.c` 将 `_mm256_set1_epi64x(target)` 提升到循环外 | 优化 | 5 分钟 | D-115 | ✅ 完成 |
| ACT-30 | 修复 TODO-05: 项目根目录创建 `.gitignore`，排除 `*.o` `*.a` `*.exe` `*.gch` 等 | 配置 | 10 分钟 | D-115 | ✅ 完成 |
| ACT-31 | 安全警告: README.txt 和 `include/int64_search.h` 标注 Int64 rebuild "单线程 only" | 安全 | 15 分钟 | D-118 | ✅ 完成 |

---

## P1 行动项（本周内）

| # | 行动项 | 类型 | 工作量 | 来源决议 | 状态 |
|---|--------|------|--------|----------|------|
| ACT-32 | 实现 TODO-04: Makefile 增加 `lib-int64` / `test-int64` / `clean-int64` 目标 | 配置 | 1-2 小时 | D-115 | ✅ 完成 |
| ACT-33 | 执行 D-037 实验: MinGW `-march=native` vs `-mavx2` 对比，输出定性结论 | 调研 | 30 分钟 | D-125 | ✅ 完成 |
| ACT-34 | 调研 Clang for Windows (LLVM-MinGW) 性能对比，跑 Int32 benchmark 三条路径 | 调研 | 1 天 | D-125 | ✅ 完成 |
| ACT-35 | 建立 GitHub Actions CI 最小流水线（Linux GCC: test + ASan/UBSan + TSan） | 配置 | 1-2 天 | D-124 | ✅ 完成 |
| ACT-36 | TODO-06: 为 Int64 库增加 10M uniform 性能回归测试 | 测试 | 2-4 小时 | ARCH | ✅ 完成 |
| ACT-37 | TODO-07: 为 Int64 库增加 zipf α=1.0 退化场景测试（验证阈值 409 回退） | 测试 | 2-4 小时 | ARCH | ✅ 完成 |
| ACT-38 | 启动 Int64 Phase 2 立项: 创建 ALIGNMENT 文档，聚焦 COW + Bloom 重建 + broadcast hoisting | 设计 | 2-4 小时 | D-116 | ⬜ 待执行 |
| ACT-39 | 热键缓存 POC: 独立快速验证（~200 行），目标 Zipf 场景 ≥2.0x vs scalar | POC | 1-2 天 | D-122 | ⬜ 待执行 |
| ACT-40 | Huge Pages POC: `perf` 确认 TLB miss 占比 → 2MB 对齐分配 → benchmark 对比 | POC | 1-2 天 | D-129 | ⬜ 待执行 |

---

## P2 行动项（有资源时）

| # | 行动项 | 类型 | 工作量 | 来源决议 |
|---|--------|------|--------|----------|
| ACT-41 | CI 扩展: 加入 Clang 编译矩阵 + Valgrind leak check + Fuzz target | 配置 | 2-3 天 | D-124 |
| ACT-42 | Benchmark 可视化: Python+matplotlib 脚本（CSV → 柱状图） | 工具 | 0.5 天 | -- |
| ACT-43 | Benchmark JSON/CSV 输出标准化 + CI 集成性能回归检测 | 工具 | 1-2 天 | -- |
| ACT-44 | 跨编译器测试矩阵: GCC 9/11/13 + Clang 15/17 | 测试 | 2-3 天 | -- |
| ACT-45 | Int64 per-bucket fallback 在生产数据上的表现评估 | 调研 | 1 天 | D-122 |
| ACT-46 | Int64 find_range B1 路径实现（等待业务需求驱动） | 实现 | 3-5 天 | D-128 |

---

## 远期 / 已归档（不执行）

| # | 行动项 | 状态 | 来源 |
|---|--------|------|------|
| — | AVX-512 实现 (TODO-15) | `[DEBT-AVX512]` 关闭 | D-119 |
| — | Eytzinger 重新评估 | `[DEBT-Eytzinger]` 归档 | D-121 |
| — | RMI POC | `[DEBT-RMI]` 不推荐 | D-123 |
| — | ARM NEON 移植 | `[DEBT-NEON]` 远期 | D-120 |
| — | int32_t dir → int64_t dir | `[YAGNI]` 等待需求 | D-127 |
| — | MSVC 编译器支持 | P3 独立评估 | -- |

---

## 统计

| 优先级 | 总数 | 已完成 | 待执行 | 总工作量估算 |
|--------|------|--------|--------|-------------|
| P0 | 5 | 5 | 0 | ~40 分钟 |
| P1 | 9 | 3 | 6 | ~5-9 天 |
| P2 | 6 | 0 | 6 | ~8-12 天 |
| 归档 | 6 | — | — | — |
| **总计** | **26** | **8** | **12** | — |
