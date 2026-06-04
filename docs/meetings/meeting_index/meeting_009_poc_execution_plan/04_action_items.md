---
title: 行动清单 — POC 执行规划会
meeting_id: meeting_009_poc_execution_plan
status: Frozen
created_at: 2026-05-30
updated_at: 2026-06-01
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_009_poc_execution_plan/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/03_decisions.md
tags: [poc, action-items, phase2]
---

# 行动清单 — POC 执行规划会

> 会议产出 7 项决议（D-071~D-077），以下为从决议派生的可执行行动项。

---

## Step 1：`src/poc_b1_fixed.c` — 修复版 B1 + 正确性验证

| 编号 | 决议 | 行动 | 状态 |
|:--:|:--:|------|:--:|
| FIX-01 | D-073 | 修复 BUG-02：去 `^ 0xFF`，`le_count` → `gt_count`，交换 `==8`/`==0` 分支 | ✅ |
| FIX-02 | D-073 | BUG-01 防御性加固：后向填充末尾显式 `dir[65536] = (int32_t)n` | ✅ |
| FIX-03 | D-073 | BUG-03 防御性检查：`h32 = (uint32_t)target >> 16; if (h32 >= 65536) return -1;` | ✅ |
| FIX-04 | D-066 | 增强 `dir_validate`：新增 `dir[0] == 0` 检查和范围约束 `dir[i] <= n` | ✅ |
| FIX-05 | D-076 | 正确性验证流程：100% hit / 0% hit / `bsearch` 交叉对比 / 边界 key | ✅ |
| FIX-06 | D-076 | ASan/UBSan 测试：n ∈ {0,1,5,63,64,65,256,65535,65536,65537,100000} | ✅ |
| FIX-07 | D-072 | POC 编译命令写入文件头注释 + README.txt | ✅ |
| FIX-08 | D-074 | Benchmark：1.5M/5M/10M × uniform/skewed × 50% hit × 2 算法（B1 + Scalar） | ✅ |

---

## Step 2：`src/poc_b1_pool.c` — 内存池 B1 + 三分对比

| 编号 | 决议 | 行动 | 状态 |
|:--:|:--:|------|:--:|
| POOL-01 | D-075 | 实现 `b1_pool_build(vals, n)`：栈临时 dir → 校验 → `_mm_malloc` → memcpy → 填充 | ✅ |
| POOL-02 | D-075 | 实现 `search_b1_pool(pool, vals, n, target)`：基址 + 偏移 lo16 扫描 | ✅ |
| POOL-03 | D-075 | 实现 `b1_pool_destroy(pool)` | ✅ |
| POOL-04 | D-075 | 定义宏：`B1_POOL_DIR` / `B1_POOL_LO16` / `B1_POOL_TOTAL_SIZE` | ✅ |
| POOL-05 | D-074 | 三分对比 benchmark：三指针 B1 vs 内存池 B1 vs 标量，1.5M/5M/10M | ✅ |
| POOL-06 | D-074 | 轮换顺序 + dummy pass + 7 repeats 中位数 | ✅ |
| POOL-07 | D-072 | 编译命令写入文件头注释 + README.txt | ✅ |

---

## Step 3：`src/poc_b1_crossover.c` — D-015 散点采集

| 编号 | 决议 | 行动 | 状态 |
|:--:|:--:|------|:--:|
| CROSS-01 | D-074 | B 级受控构造：M ∈ {1,2,5,10,20,50,100,200,500,1K,2K,5K,10K,20K,50K,100K,n/2,n} | ✅ |
| CROSS-02 | D-074 | A 级自然验证：uniform + skewed, 5 规模，每组合 1 点 | ✅ |
| CROSS-03 | D-074 | 标量二分每轮重测（不共用第一轮值） | ✅ |
| CROSS-04 | D-074 | 输出 CSV：`n,max_bucket,b1_pool_cy_q,b1_3ptr_cy_q,scalar_cy_q,pool_vs_scalar,3ptr_vs_scalar` | ✅ |
| CROSS-05 | D-072 | 编译命令写入文件头注释 + README.txt | ✅ |

---

## 会议收尾

| 编号 | 决议 | 行动 | 状态 |
|:--:|:--:|------|:--:|
| CLOSE-01 | — | 更新 `docs/meetings/meeting_index.md` 注册 meeting_009 | ✅ |
| CLOSE-02 | — | meeting_009 所有文档最终状态同步 | ✅ |

---

> 状态标记：⬜ PENDING / 🟡 RUNNING / ✅ DONE / ❌ FAILED / 🚫 BLOCKED
