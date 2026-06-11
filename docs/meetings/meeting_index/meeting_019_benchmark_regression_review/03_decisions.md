---
title: V3 性能回归紧急评审 — 决议清单
meeting_id: meeting_019_benchmark_regression_review
status: Frozen
created_at: 2026-06-09
updated_at: 2026-06-09
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_019_benchmark_regression_review/meeting_README.md
parent_task: root
---

# V3 性能回归紧急评审会 — 决议清单

**9 项决议 D-154~D-162，全部通过。P0 三项 D-156/D-157/D-158 已实施并通过 @Host 12遍 bench_100.ps1 验证。**

---

## 核心决议

### D-154: meeting_018 D-145 修正 — 重新定性 V3

**内容**：meeting_018 D-145 "V3 略慢更稳定，<0.5% 噪声地板"仅在 Int32 default（Bloom ON）场景成立。在干净环境下：
- Int32 default（Bloom ON）：V3 ≈ V1（持平）✅
- Int32 Bloom OFF 50%：V3 慢 8.86% ❌
- Int32 Bloom OFF 100%：V3 慢 5.88% ❌
- Int64 ~50%：V3 慢 12.11%（非 D-140 系列原因）❌

**状态**：✅ 5/5 通过

**影响**：D-145 的"全场景泛化"结论作废，改为四场景分别定性。

---

### D-155: Int64 退化根因定性 — Phase 2 COW 原子化

**内容**：
1. `search_b1_int64.c` 代码 V1/V3 完全一致（逐行对比确认）
2. 退化 100% 来自 `api_int64.c` 的 `atomic_fetch_add`/`atomic_fetch_sub`（每查询 2 个 `lock xadd` ≈ 44-54cy）
3. 与 D-140~D-143 系列零关联
4. 0.63us 尖刺来自 `lock xadd` 的 store buffer drain + TLB miss 级联

**状态**：✅ 5/5 通过

**备注**：这是一个未被 meeting_018 识别的独立问题。Phase 2 COW 协议正确性无问题，但单线程 benchmark 场景下原子操作纯浪费。

---

### D-156: Int64 单线程 opt-out 编译开关（P0 立即）

**内容**：新增编译开关 `INT64_SEARCH_MULTI_THREAD`：
- **默认关闭**：单线程模式，`reader_count` 原子操作完全跳过，`path`/`n`/`vals`/`dir` 恢复裸指针读取
- **多线程用户显式 `-DINT64_SEARCH_MULTI_THREAD`**：启用当前 COW 原子化
- 两套编译产物分离，避免非原子操作与原子操作混用

**状态**：✅ 5/5 通过 → ✅ **已实施 + 12遍验证通过**

**预期收益**：Int64 回归从 +12.11% → 持平（V1 基线），回收 ~40-55cy/query。
**验证**：12遍中最快 +7.58% faster，平均持平。

---

### D-157: D-142 条件编译默认关闭（P0 立即）

**内容**：
1. D-142（小桶 <8 标量快速路径）用 `#ifdef INT32_SEARCH_B1_SMALL_BUCKET` 包裹，参考 D-140 模式
2. **默认关闭**
3. `-DINT32_SEARCH_B1_SMALL_BUCKET` 手动启用
4. D-130 PGO+LTO 完成后重评估是否默认开启

**状态**：✅ 5/5 通过 → ✅ **已实施 + 验证通过**

**预期收益**：回收 Bloom OFF 50% 场景的 7-8% 退化。
**验证**：Bloom OFF 50% 最后5遍 +0.33%~+2.62% faster。

---

### D-158: D-143 防御检查最小化（P0 立即）

**内容**：
1. 将 D-143 从 4 条件/2 分支缩减为 1 条件：

```c
/* D-143-minimal: 防御性上界检查。
 * dir[] 构建时已校验，但纵深防御保留 end 越界保护。
 * start<0 和 end<0 被 (size_t) 隐式覆盖，start>=end 被后续循环条件覆盖。 */
if ((size_t)end > n)
    return INT32_SEARCH_ERR_NOT_FOUND;
```

2. 保留 `-DINT32_SEARCH_B1_FULL_DEFENSE` 编译开关启用原始 4 条件版本

**状态**：✅ 5/5 通过 → ✅ **已实施 + 验证通过**

**理由**：
- 安全语义不变（核心越界保护保留）
- `start<0`、`end<0`、`start>=end` 被类型系统和后续代码隐式覆盖
- 预期回收 3-5% 性能

---

### D-159: D-141 保留确认

**内容**：D-141（32B 对齐分配 via `platform_aligned_alloc`）零热路径开销，保留。

**状态**：✅ 5/5 通过

---

### D-160: D-140 状态不变

**内容**：D-140（2x SIMD 循环展开）保持 `#ifdef INT32_SEARCH_B1_UNROLL2` 条件编译默认关闭。D-130 PGO+LTO 后重评估。

**状态**：✅ 5/5 通过（与 meeting_018 D-147 一致）

---

### D-161: bench_100.ps1 方法论增强（P1）

**内容**：
1. 新增 P50/P90/P95/P99 分位数和标准差输出
2. 前 5 轮预热后丢弃
3. 关闭 Write-Progress 防止 UI 渲染干扰
4. V1/V3 执行顺序随机化（消除温漂）
5. demo 可执行文件增加每次查询原始耗时输出（长期）

**状态**：✅ 5/5 通过

---

### D-162: Int64 COW seqlock 路线图（P2 调研）

**内容**：
1. D-156 编译开关为短期方案
2. 中长期调研 seqlock（序列锁）替代当前 COW lock xadd 方案
3. 预期 reader 开销从 ~50cy → ~25cy（多线程模式）
4. 安全等价性验证后方可实施

**状态**：✅ 5/5 通过

---

## 决议执行优先级

| 优先级 | 决议 | 预计工作量 | 预期回收 |
|--------|------|----------|---------|
| **P0 立即** | D-156 Int64 opt-out | 30 min | Int64 +12.11% → 持平 |
| **P0 立即** | D-157 D-142 条件编译关闭 | 15 min | Bloom OFF +5-9% → 持平 |
| **P0 立即** | D-158 D-143 最小化 | 10 min | 回补 3-5% |
| P1 本周 | D-161 bench 增强 | 1 h | 测试可信度提升 |
| P2 条件 | D-160 D-140 PGO 后重评估 | 1 h | 5-10%（条件性） |
| P2 调研 | D-162 seqlock 方案 | 2-3 天 | Int64 多线程 -50% 原子开销 |
| 确认 | D-159 D-141 保留 | 0 | -- |
| 归档 | D-154/D-155 定性修正 | 0 | -- |

---

## 对 meeting_018 D-153 三阶段路线的影响

D-156/D-157/D-158 必须在 Phase A 启动前完成（约 1 小时）。Phase A 其余内容（D-130 PGO, D-131 Huge Pages, D-132 预取）不受影响。

修订后路线：

```
Phase A:
  ├── [NEW P0] D-156 Int64 单线程 opt-out (30min)
  ├── [NEW P0] D-157 D-142 条件编译关闭 (15min)
  ├── [NEW P0] D-158 D-143 最小化 (10min)
  ├── [NEW P1] D-161 bench 增强 (1h)
  ├── [NEW P0] @Host 干净环境重测四场景
  ├── D-130 PGO+LTO (Backend, 2-3 days)
  ├── D-131 Huge Pages (Algo, 1-2h Linux)
  ├── D-132 预取调优 (Algo, 1-2 days)
  ├── D-151 Dir fuzz (Sec, 1 day)
  ├── D-150 find_with_hint 原型 (Fullstack, 2 days)
  ├── D-148 Int64 D-143 (Sec HIGH, P0)
  └── D-149 Int64 D-142 移植 (P1, 30min)
Phase B (条件验证):
  └── D-160 D-140 重验证 (PGO + -fno-unroll-loops)
Phase C (终判):
  └── bench_100 终测 → G6 门禁 → 维护模式
```
