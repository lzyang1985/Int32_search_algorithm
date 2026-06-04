---
title: 专家讨论记录 — POC 执行规划会
meeting_id: meeting_009_poc_execution_plan
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_009_poc_execution_plan/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_008_b1_memory_pool/03_decisions.md
  - docs/meetings/meeting_index/meeting_008_b1_memory_pool/04_action_items.md
  - src/poc_benchmark_v3.c
tags: [poc, memory-pool, b1, execution-plan, discussion]
---

# 专家讨论记录 — POC 执行规划会

## 1. POC 文件拆分策略

### 1.1 架构师意见：方案 C（三文件结构）

三个独立 POC 文件，精确对应 D-070 行动链：

```
poc_b1_fixed.c      → ACT-01~04（修 Bug + 可信基线）
poc_b1_pool.c       → ACT-05（内存池 + 三向对比）
poc_b1_crossover.c  → ACT-06（D-015 散点采集）
```

**文件到库源码的映射**：

| POC 文件 | 库模块 |
|----------|--------|
| `poc_b1_fixed.c` | `build_dir.c` + `search_b1.c`（三指针版） |
| `poc_b1_pool.c` | `build_b1.c` + `search_b1.c`（池版） |
| `poc_b1_crossover.c` | `build_decision.c` |

**公共代码策略**：跨文件复制，禁止共享 `.h`。每个文件内联 ~70 行基础设施。与现有 `poc_eytzinger.c` / `poc_stree.c` 实践一致。

### 1.2 全票通过

| Agent | 立场 |
|-------|:--:|
| Architect | ✅ 方案 C |
| Algorithm | ✅ 支持三文件（每个文件独立 benchmark） |
| Backend | ✅ 支持三文件（独立编译、可交叉对比） |
| Security | ✅ 支持三文件（每个文件独立 ASan 测试） |

---

## 2. Benchmark 方法设计

### 2.1 算法工程师方案

**同进程 + 轮换顺序对抗排序效应**：6 轮轮换，算法间跑 dummy pass 刷缓存。取 7 repeats 中 discard 前 3 后的中位数 + stddev。

**规模梯度**：

| n | queries | warmup |
|---|:--:|:--:|
| 100K | 200K | 1000 |
| 500K | 500K | 5000 |
| 1.5M | 1M | 10000 |
| 5M | 1M | 10000 |
| 10M | 1M | 10000 |

**D-015 散点**：主动构造受控数据，M ∈ {1,2,5,10,20,50,100,200,500,1K,2K,5K,10K,20K,50K,100K,n/2,n}

**输出**：stdout 人类可读 + CSV 文件（带 header）

### 2.2 讨论

| 议题 | Architect | Algorithm | Backend | Security |
|------|-----------|-----------|---------|----------|
| 轮换顺序 | ✅ | ✅ 设计者 | ✅ 实用 | ✅ |
| 中位数 vs 最小值 | 中位数 | 中位数 | 中位数 | 中位数 |
| D-015 受控构造 | 可行 | 设计者 | 可工程化 | ✅ |

---

## 3. BUG 修复策略

### 3.1 安全专家精确定位

| Bug | 根因 | 修复方法 | 修改行数 |
|-----|------|----------|:--:|
| **BUG-01** | 静态分析无法精确复现 `dir[65536] != n`。建议 ASan 动态验证 + 防御性加固（后向填充后再显式设置 `dir[65536] = n`） | 1 行防御性代码 | 1 |
| **BUG-02** | `le_count = popcount(mask ^ 0xFF)` 语义取反 + 分支边界条件（lo/hi）完全反向 | 去掉 `^ 0xFF`，`le_count` → `gt_count`，交换 `==8`/`==0` 分支 | 3 行 |
| **BUG-03** | B1 路径 L161 已安全（`(uint32_t)target >> 16`）。风险在生产代码。B1 入口加防御性范围检查 | 1 行防御性检查 | 1 |

### 3.2 BUG-02 详细修复（安全专家确认）

**不改变** `cmpgt(k, v)`，仅去掉 `^ 0xFF`：

```
修复前: le_count = popcount(mask ^ 0xFF)      // 语义错误
        if (le_count == 8) lo = block + 8;    // 方向反了
        if (le_count == 0) hi = block;        // 方向反了

修复后: gt_count = popcount(mask)              // target > element 的元素数
        if (gt_count == 0) hi = block;        // target <= 全部元素
        if (gt_count == 8) lo = block + 8;    // target > 全部元素
```

### 3.3 讨论

| Agent | BUG-01 可复现性 | BUG-02 修法 | BUG-03 |
|-------|:--:|:--:|:--:|
| Security | 静态无法确认，建议降级 MEDIUM | `cmpgt(k,v)` 保留，去 `^0xFF` | B1 安全，加固即可 |
| Backend | 在临时 dir + 后向填充后显式重置哨兵 | 支持 | 支持 |
| Algorithm | 先修再测，ASan 验证 | 必须修（hits=0） | 支持 |

---

## 4. 内存池 C 实现细节

### 4.1 后端工程师方案

| 决策点 | 推荐 |
|--------|------|
| 内存池表示 | `uint8_t *pool` + 偏移宏（不声明结构体） |
| Padding | `memset(pool + 262148, 0, 28)` |
| `_Alignas(32)` | 不需要（`_mm_malloc` 保证） |
| 临时 dir | 栈上 `int32_t temp_dir[65537]`（256KB） |
| D-015 决策 | POC 阶段不实现 |
| 查询签名 | `search_b1_pool(pool, vals, n, target)` |

### 4.2 辅助宏

```c
#define B1_POOL_DIR(pool)     ((const int32_t *)(pool))
#define B1_POOL_LO16(pool)    ((const uint16_t *)((const char *)(pool) + 262176))
#define B1_POOL_TOTAL_SIZE(n) (262176 + 2 * (size_t)(n))
```

### 4.3 构建流程

```
b1_pool_build(vals, n):
  1. n == 0 → NULL
  2. 栈分配 temp_dir[65537]
  3. high16_dir_build(vals, n, temp_dir)  // 修复版
  4. dir_validate(temp_dir, n) → 失败: NULL
  5. pool = _mm_malloc(262176 + 2*n, 32)
  6. memcpy(pool, temp_dir, 262148)
  7. memset(pool + 262148, 0, 28)
  8. lo16[i] = vals[i] & 0xFFFF
  9. return pool
```

### 4.4 全票通过

所有 Agent 同意上述设计。

---

## 5. 编译命令与验证方案

### 5.1 编译命令（Backend + Architect 共识）

```bash
# POC 基准编译
gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/poc_b1_fixed.c -o poc_b1_fixed
gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/poc_b1_pool.c -o poc_b1_pool
gcc -O3 -std=c11 -mavx2 -march=native -Wall -Wextra -fno-tree-vectorize \
    src/poc_b1_crossover.c -o poc_b1_crossover
```

`-fno-tree-vectorize` 是**关键选项**：防止 GCC 自动向量化标量尾部循环，确保测量的是手写 SIMD 精确性能。

### 5.2 ASan 验证命令（Security + Backend）

```bash
gcc -O1 -g -std=c11 -mavx2 -march=native \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    src/poc_b1_fixed.c -o poc_b1_fixed_asan
```

### 5.3 正确性验证（Security）

修复后必须与 `bsearch()` 和 `search_scalar_bs` 交叉对比：
- n ∈ {100, 10000, 100000} × hit rate {0%, 50%, 100%}
- 含 INT32_MIN / -1 / 0 / INT32_MAX 边界 key
- 验证 BUG-02 修复后 hits 数 = queries × hit_rate

---

## 6. 交叉讨论：争议点

### 6.1 BUG-01 严重度是否需要降级？

| Agent | 观点 |
|-------|------|
| Security | 静态分析无法复现 → 建议 MEDIUM，加防御性代码而非"修复" |
| Backend | 同意。在 `high16_dir_build` 末尾显式 `dir[65536] = n` 即可 |
| Algorithm | 无所谓，防御性加固不增加 benchmark 开销 |
| Architect | 同意降级。但如果 ASan 下复现了 bug，立即恢复 HIGH |

### 6.2 D-015 散点采集：受控构造 vs 自然分布？

| Agent | 观点 |
|-------|------|
| Algorithm | **推荐受控构造**（M 序列扫描），因为 uniform 只能拿到一个点 |
| Architect | 支持。但必须也在自然分布（uniform/skewed）上验证构造数据的结论 |
| Security | 受控构造可能引入排序异常，需验证构造后的数据仍然有序 |

**决议倾向**：B 级（受控构造）做散点曲线，A 级（自然分布）做 15 个点的验证。

---

## 7. 讨论总结

4/4 Agent 在所有核心议题上达成一致。主要成果：

1. ✅ **三文件结构**（`poc_b1_fixed.c` + `poc_b1_pool.c` + `poc_b1_crossover.c`）
2. ✅ **跨文件复制公共代码**，无共享头文件，单条 gcc 编译
3. ✅ **同进程轮换 benchmark**（6 轮排列 + dummy pass），7 repeats 取中位数
4. ✅ **BUG 修复**：BUG-02 关键修复（去 `^ 0xFF`），BUG-01 防御性加固，BUG-03 防御性检查
5. ✅ **内存池实现**：`uint8_t *pool` + 3 个宏 + 栈临时 dir + `memset` padding
6. ✅ **编译选项**：`-O3 -fno-tree-vectorize`；ASan：`-O1 -fsanitize=address,undefined`
7. ⚠️ **BUG-01 严重度降为 MEDIUM**（静态无法复现，ASan 动态验证），加防御性代码

---

> 以上讨论记录待生成决议文档（03_decisions.md）。
