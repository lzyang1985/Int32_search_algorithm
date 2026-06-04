---
title: 行动清单 — B1 内存池方案讨论会
meeting_id: meeting_008_b1_memory_pool
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_008_b1_memory_pool/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_008_b1_memory_pool/03_decisions.md
tags: [b1, memory-pool, action-items, phase2]
---

# 行动清单 — B1 内存池方案讨论会

## P0（阻塞项 — 必须在内存池 POC 前完成）

| 编号 | 决议 | 行动 | 产出 | 状态 |
|:--:|:--:|------|------|:--:|
| ACT-01 | D-065 | 修复 `high16_dir_build` 后向填充 off-by-one bug（BUG-01） | 修复后的 `poc_benchmark_v3.c` 或新文件 `poc_b1_fixed.c` | ⬜ |
| ACT-02 | D-065 | 修复 AVX2 搜索逻辑中 `cmpgt(key,vec)` 语义错误（BUG-02） | 正确的 `cmpgt(vec,key)` 或等效逻辑 | ⬜ |
| ACT-03 | D-065 | 修复符号扩展漏洞（BUG-03）：`(uint32_t)key >> 16` + `h < 65536` 检查 | 安全加固后的 B1 搜索路径 | ⬜ |
| ACT-04 | D-065/070 | 用修复后的 B1 重新 benchmark（1.5M/5M/10M, uniform/skewed） | `src/poc_b1_fixed.c` + benchmark 数据 | ⬜ |

## P1（高优先级 — 核心交付）

| 编号 | 决议 | 行动 | 产出 | 状态 |
|:--:|:--:|------|------|:--:|
| ACT-05 | D-060/061 | 实现单内存池 B1 POC（单文件自包含，`src/poc_b1_pool.c`） | 池化 B1 实现 + benchmark 对比 | ⬜ |
| ACT-06 | D-068 | 采集 `max_bucket → cy/q` 散点数据（5 种规模 × 3 种分布） | D-015 crossover 数据表 | ⬜ |
| ACT-07 | D-066 | 实现增强版 `dir_validate`（4 项检查） | 带 4 项校验的验证函数 | ⬜ |
| ACT-08 | D-070 | 对比：三指针 B1 vs 内存池 B1 vs 标量二分（同组数据） | 可信加速比数据 | ⬜ |

## P2（中等优先级 — 库源码合并）

| 编号 | 决议 | 行动 | 产出 | 状态 |
|:--:|:--:|------|------|:--:|
| ACT-09 | D-067 | 更新 `src/internal.h` 结构体（新增 pool/pool_size，移除 search_fn） | 新 `int32_search_impl_t` | ⬜ |
| ACT-10 | D-064 | 改造 `api.c` 为 `switch(path)` 分发（`int32_search_find` / `_create` / `_destroy`） | 支持 PATH_B1 dispatch 的 api.c | ⬜ |
| ACT-11 | D-060 | 新建 `src/build_dir.c` — high16 directory 构建 + 4 项校验 | 可独立编译的构建模块 | ⬜ |
| ACT-12 | D-060 | 新建 `src/build_b1.c` — 内存池分配 + dir 填充 + lo16 填充 | 可独立编译的构建模块 | ⬜ |
| ACT-13 | D-060 | 新建 `src/search_b1.c` — pool 偏移式 B1 搜索（含 BUG-03 修复） | 可独立编译的搜索模块 | ⬜ |
| ACT-14 | D-068 | 新建 `src/build_decision.c` — D-015 分布分析 + 路径决策（阈值待校准） | 可独立编译的决策模块 | ⬜ |
| ACT-15 | D-060 | 更新 Makefile / README.txt 编译命令 | 新 .o 文件的编译规则 | ⬜ |

## P3（低优先级 — 验证与扩展）

| 编号 | 决议 | 行动 | 产出 | 状态 |
|:--:|:--:|------|------|:--:|
| ACT-16 | D-066 | ASan/UBSan 测试：n ∈ {0,1,5,63,64,65,65535,65536,65537} | 零告警报告 | ⬜ |
| ACT-17 | — | 更新 `docs/architecture/技术路线.md` 反映 D-060~D-069 决议 | 技术路线 v1.1 | ⬜ |
| ACT-18 | D-069 | Phase 1.5 COW 从 seq_lock 迁移到无锁单指针（pool 快照） | 无锁 B1 COW 实现 | ⬜ |

---

> 状态标记：⬜ PENDING / 🟡 RUNNING / ✅ DONE / ❌ FAILED / 🚫 BLOCKED
