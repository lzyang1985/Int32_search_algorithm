---
title: 决议文档 — POC 执行规划会
meeting_id: meeting_009_poc_execution_plan
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_009_poc_execution_plan/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/02_discussion.md
  - docs/meetings/meeting_index/meeting_008_b1_memory_pool/03_decisions.md
tags: [poc, execution-plan, decision, phase2]
---

# 决议文档 — POC 执行规划会

> 4/4 专家参与。以下决议草案待人工确认 Frozen。

---

## D-071：采用三文件 POC 结构

**投票**：4/4 通过

| 文件 | 对应 meeting_008 行动 | 内容 |
|------|:--:|------|
| `src/poc_b1_fixed.c` | ACT-01~04 | 修复 3 项 bug 后的三指针 B1 + 标量二分对比 + 正确性验证 |
| `src/poc_b1_pool.c` | ACT-05, ACT-08 | 内存池 B1 + 三指针 B1 + 标量三分对比 + D-066 dir_validate |
| `src/poc_b1_crossover.c` | ACT-06 | D-015 散点采集（5 规模 × M 序列 + 自然分布验证） |

**约束**：
- 每个文件单文件自包含（D-055），仅 include 标准库头文件
- 公共代码（数据生成、计时框架等约 70 行）跨文件内联复制，不创建共享 `.h`
- 所有内部函数标 `static`
- 单条 `gcc` 命令编译，不依赖 Makefile

---

## D-072：编译命令规范

**投票**：4/4 通过

```bash
# POC 基准编译（性能测量用）
gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/poc_b1_fixed.c -o poc_b1_fixed
gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/poc_b1_pool.c -o poc_b1_pool
gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/poc_b1_crossover.c -o poc_b1_crossover

# ASan/UBSan 验证编译（正确性验证用）
gcc -O1 -g -std=c11 -mavx2 -march=native -fsanitize=address,undefined \
    -fno-omit-frame-pointer src/poc_b1_fixed.c -o poc_b1_fixed_asan
```

`-fno-tree-vectorize` 理由：防止 GCC 自动向量化标量尾部循环，确保测量手写 SIMD 精确性能。

---

## D-073：BUG 修复最终方案

**投票**：4/4 通过

### BUG-02（CRITICAL）— AVX2 搜索语义修复

```c
// 只改 3 行
int gt_count = __builtin_popcount(mask);  // 去 ^ 0xFF，变量改名

if (gt_count == 0)       hi = block;      // target <= 全部元素
else if (gt_count == 8)  lo = block + 8;  // target > 全部元素
else {
    size_t idx = block + (size_t)gt_count;
    ...
}
```

不改 `cmpgt(k, v)`（语义正确），只修复 popcount 取反和分支方向。

### BUG-01（MEDIUM，降级）— DIR 后向填充防御性加固

静态分析无法精确复现 `dir[65536] != n` 触发条件。改为**防御性加固**而非语义修复：

在 `high16_dir_build` 末尾（后向填充之后）加一行：
```c
dir[DIR_BUCKETS - 1] = (int32_t)n;  // 显式重置哨兵
```

ASan 编译后跑 `n=65537` 动态验证。若能复现 bug，恢复 HIGH 严重度。

### BUG-03（CRITICAL）— 符号扩展防御性检查

B1 路径 L161（`(uint32_t)target >> 16`）已安全，但为生产代码安全示范，在 B1 搜索入口加：

```c
uint32_t h32 = (uint32_t)target >> 16;
if (h32 >= 65536) return -1;  // 防御性检查
uint16_t h = (uint16_t)h32;
```

---

## D-074：Benchmark 方法学

**投票**：4/4 通过

| 参数 | 值 |
|------|-----|
| 进程模型 | 同进程，6 轮轮换顺序（每个算法各做 2 次首发/次发/末发） |
| 算法间隔离 | 跑 2MB dummy pass 刷 L2 缓存 |
| 每算法 repeats | 7 次，discard 前 3，取后 4 的中位数 |
| 输出指标 | cy/q_median, stddev, min, max, speedup_vs_scalar |
| 输出格式 | stdout 人类可读 + CSV 文件（带 header） |

**规模梯度**：

| n | queries | warmup |
|---|:--:|:--:|
| 100K | min(200K, 2n) | max(1000, n/100) |
| 500K | min(500K, 2n) | max(1000, n/100) |
| 1.5M | 1M | 10000 |
| 5M | 1M | 10000 |
| 10M | 1M | 10000 |

**D-015 散点采集（`poc_b1_crossover.c`）**：
- **B 级（受控构造）**：M ∈ {1,2,5,10,20,50,100,200,500,1K,2K,5K,10K,20K,50K,100K,n/2,n}，画 M → cy/q 散点
- **A 级（自然分布验证）**：uniform + skewed，5 规模，每组合一个验证点
- 标量二分必须每轮重测（不可只用第一轮值），用于计算加速比

---

## D-075：内存池 POC 实现规范

**投票**：4/4 通过

**表示方式**：`uint8_t *pool` + 三个宏

```c
#define B1_POOL_DIR_SIZE      262148
#define B1_POOL_LO16_OFFSET   262176
#define B1_POOL_TOTAL_SIZE(n) (B1_POOL_LO16_OFFSET + 2 * (size_t)(n))

#define B1_POOL_DIR(pool)     ((const int32_t *)(pool))
#define B1_POOL_LO16(pool)    ((const uint16_t *)((const char *)(pool) + B1_POOL_LO16_OFFSET))
```

**构建函数**：`uint8_t *b1_pool_build(const int32_t *vals, size_t n)`
- 栈分配 `int32_t temp_dir[65537]`（256KB，POC 安全）
- 修复版 `high16_dir_build` + 增强版 `dir_validate`（D-066）
- `_mm_malloc(262176+2n, 32)` → `memcpy` dir → `memset(pool+262148, 0, 28)` → 填充 lo16
- 失败返回 NULL

**查询函数**：`int32_t search_b1_pool(const uint8_t *pool, const int32_t *vals, size_t n, int32_t target)`
- lo16_base 函数内计算（一条 LEA 指令，零内存访问）
- 与三指针版 `search_b1_high16_lo16` 的 lo16 扫描逻辑完全一致

**销毁函数**：`void b1_pool_destroy(uint8_t *pool)` → `_mm_free(pool)`

---

## D-076：正确性验证方案

**投票**：4/4 通过

POC 编译后、benchmark 前，必须通过以下验证（在 `poc_b1_fixed.c` 中实现）：

| 步骤 | 验证内容 | 方法 |
|:--:|------|------|
| 1 | dir 构建正确性 | `dir_validate`（D-066 增强版 4 项检查） |
| 2 | B1 命中率验证 (n=100K, 100% hit) | hits == n（全命中） |
| 3 | B1 漏检率验证 (n=100K, 0% hit) | hits == 0（全漏检） |
| 4 | 与标量二分交叉验证 (n=100K, 50% hit) | B1 与 `search_scalar_bs` 逐查询结果一致 |
| 5 | 边界 key 验证 | INT32_MIN, -1, 0, 1, INT32_MAX 不崩溃且结果正确 |
| 6 | ASan/UBSan 零告警 | n ∈ {0,1,5,63,64,65,256,65535,65536,65537,100000} |

对比基线：`bsearch()` 和 `search_scalar_bs`。

---

## D-077：POC 执行顺序与交付物

**投票**：4/4 通过

| 顺序 | 文件 | 预估行数 | 产出 | 状态 |
|:--:|------|:--:|------|:--:|
| **Step 1** | `src/poc_b1_fixed.c` | ~300 行 | 可信 B1 三指针基线数据 + 正确性验证 | ⬜ |
| **Step 2** | `src/poc_b1_pool.c` | ~350 行 | 三指针 vs 内存池 vs 标量 三分对比数据 | ⬜ |
| **Step 3** | `src/poc_b1_crossover.c` | ~300 行 | D-015 crossover 散点 + 自然分布验证 | ⬜ |

**每个 Step 的出入口标准**：
- 入口：前一步通过正确性验证
- 出口：benchmark 数据可信（stddev < 5% 中位数，否则标记 `[NOISY]`）

**Step 1 验证通过后，Step 2 和 Step 3 可并行执行。**

**更新 README.txt**：每步完成后追加编译命令到 README.txt。

---

> 以上决议草案待人工审阅确认 Frozen。确认后将生成 04_action_items.md 详细行动清单。
