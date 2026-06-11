---
title: V3 性能回归紧急评审 — 讨论记录
meeting_id: meeting_019_benchmark_regression_review
status: Frozen
created_at: 2026-06-09
updated_at: 2026-06-09
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_019_benchmark_regression_review/meeting_README.md
parent_task: root
---

# V3 性能回归紧急评审会 — 讨论记录

## 参与方

| 角色 | Agent | 代号 |
|------|-------|------|
| 架构师/项目经理 | architect-project-manager | **Arch** |
| 算法工程师 | algorithm-engineer | **Algo** |
| 后端/性能工程师 | backend-engineer | **Backend** |
| 代码安全专家 | code-security-expert | **Sec** |
| 全栈开发/系统集成 | fullstack-dev | **Fullstack** |
| @Host | Human | **Host** |

---

## 议题1: meeting_018 为何与实际矛盾？

**5/5 一致结论：meeting_018 存在三重盲区，而非分析错误。**

### 盲区一：单场景结论被错误泛化

所有 Agent 一致指出：meeting_018 的 D-145 "V3 略慢更稳定，<0.5% 噪声"仅基于 Int32 default（Bloom ON）场景的实测，却被写成了全场景决议。

| Agent | 关键论点 |
|-------|---------|
| **Algo** | "SNR=0.14 计算仅适用于 Int32 default 场景，完全不能外推到 Int64（有 ~50cy 原子增量）和 Bloom OFF（无 Bloom 稀释）" |
| **Arch** | "meeting_018 结论在其约束条件下（Int32 default + 有噪声环境）正确，但被错误当成全场景结论——评审流程缺陷" |
| **Fullstack** | "将三个未测试场景的结论写成统一决议，是测试方法论的根本缺陷" |
| **Backend** | "单点数据过度泛化，对 Int64 的讨论停留在纸面推演，从未实测" |
| **Sec** | "不干净的测试环境掩盖了真实代价" |

### 盲区二：测试环境噪声地板过高

meeting_018 环境有后台负载，噪声地板 3-5%。@Host 干净环境下噪声 <1%，5-12% 退化立刻浮现。

### 盲区三：Int64 Phase 2 COW 原子化被完全忽略

meeting_018 在议题2讨论 Int64 时，只涉及"缺 D-143 防御"和"D-142 阈值选 4"，完全没有分析 Int64 从 Phase 1 → Phase 2 引入的 6 次原子操作。

**Algo 关键发现**：`search_b1_int64.c` 代码 V1/V3 完全一致（逐行对比确认），退化 100% 来自 `api_int64.c` 的 `atomic_fetch_add`/`atomic_fetch_sub`。

### 共识

> **meeting_018 不是技术分析错误，而是测试覆盖不足 + 环境不干净 + 结论过度泛化。D-145 决议在 Int32 default 场景正确，但被错误推广到全场景。**

---

## 议题2: Int64 max 0.63us 尖刺根因

### 5/5 一致：Phase 2 COW 原子操作，与 D-140~D-143 无关

**Algo 定量分析**：

| 原子操作 | x86 指令 | Skylake 周期 |
|----------|---------|-------------|
| `fetch_add(reader_count, +1, acquire)` | `lock xadd` | 20-25cy |
| `load(path/n/vals/dir, acquire)` | `mov`（TSO 免费） | ~4cy 合计 |
| `fetch_sub(reader_count, -1, release)` | `lock sub` | 20-25cy |
| **合计** | | **~44-54cy** |

在 ~470cy 查询中占比 9.4-11.5%，实测 12.11% 吻合。

**Backend 尖刺机理解释**：

> 0.63us 来自 `lock` 指令的可变延迟：Windows benchmark 循环中 store buffer 饱和 → `lock xadd` 强制 drain（40-50cy）→ 遇 TLB miss（20-30cy）→ 两个 lock 操作级联 → 仅原子操作就 120+ cy。

**Arch 补充**：`reader_count` 与 `path`/`n` 共享缓存行，`lock xadd` 强制该行进入 Modified 状态，阻止后续 `acquire load` 的乱序执行。

### 共识

> **Int64 退化根因是 Phase 2 COW 原子化（每查询 2 个 `lock` 前缀 RMW），与 D-140~D-143 系列零关联。这是一个未被 meeting_018 识别的独立问题。**

---

## 议题3: Bloom OFF 系统性变慢根因

### Algo 定量分析

**Bloom OFF 50% hit（+8.86%）**：

| 贡献源 | 周期增量 | 占比 |
|--------|---------|------|
| D-142 分支预测失败（~30% 预测失败率 × 18cy） | ~5.4cy | ~50% |
| D-143 4 条件判断 | ~2.5cy | ~23% |
| 代码布局扰动（寄存器分配变化、循环对齐偏移） | ~3cy | ~27% |

**Bloom OFF 100% hit（+5.88%）**：

全命中路径仅 ~120cy（V1），D-142/D-143 注入约 6-7cy 固定指令开销。但实测 0.04→0.05us（+25% 相对），差值主要由新增指令扰乱乱序调度窗口 → 关键内存加载被推迟 → 间接内存时序恶化贡献。

**Backend 验证**：6-7cy / 120cy = 5-5.8%，与实测 5.88% 精确吻合，确认无隐藏陷阱。

### 分支代理分歧

| Agent | 对 D-142&D-143 的判断 |
|-------|---------------------|
| **Backend** | D-143 纯惩罚（0 性能收益），D-142 大桶场景负收益 |
| **Arch** | "在内存墙面前，减少代码体积的优先级高于计算微优化" |
| **Sec** | D-143 过度防御（4 条件对"不会发生"的场景） |
| **Algo** | D-142 回退可回收 Bloom OFF 50% 的 7-8% |
| **Fullstack** | 需要更多分位数数据确定根因 |

### 共识

> **Bloom OFF 退化由 D-142（小桶分支预测失败）+ D-143（代码膨胀）+ 间接内存时序恶化共同导致。D-142 的 ~2.1cy 理论收益被代码膨胀代价反噬。**

---

## 议题4: 内存瓶颈环境下的重新评估

### 核心悖论（Backend 提出，全体认可）

> 当 CPU 88% 时间等待内存时，增加 1 字节代码体积比增加 1 cycle CPU 指令更致命。

原因三重：
1. **ROB 槽位竞争**：D-142/D-143 增加 ~10-15 uops 占据 Reorder Buffer 224 槽位
2. **I-cache 恶性循环**：代码膨胀 → I-cache miss → "内存瓶颈"反而因代码膨胀而加剧
3. **优化方向应逆转**：省 1 字节 > 省 1 cycle

### 正确权衡矩阵

| 优化 | 省 cycles | 增代码 | 净收益 |
|------|----------|--------|--------|
| D-142（小桶标量） | 7cy（仅 <8 桶，15% 概率） | ~128B | **负**（大桶场景） |
| D-143（4 条件防御） | 0（纯防御） | ~48B | **纯惩罚** |
| D-141（32B 对齐） | 0-3cy | 0B | **正**（零开销） |
| D-140（2x 展开） | 12-20%（SIMD 扫描） | ~256B | 待 PGO 验证 |

### 共识

> **在内存瓶颈的极限场景下，微架构层面的任何指令注入都不是免费的。"<1cy"的额外指令通过改变寄存器分配、I-cache 布局、分支预测器状态、乱序调度窗口，可放大为 5-12% 退化。D-141 是唯一零代价的改动。**

---

## 议题5: 行动决策 — 5 位 Agent 投票

### Int64 路径

| Agent | 方案 | 理由 |
|-------|------|------|
| **Backend** | `INT64_THREAD_SAFE` 编译开关默认关闭 | 单线程用户零开销，多线程显式启用 |
| **Sec** | seqlock 替代 lock xadd | Reader 开销减半（~25cy vs ~50cy），安全等价 |
| **Arch** | 接受现状 + D-130 PGO 后期优化 | COW 是正确性基础设施，12% 代价可接受 |
| **Algo** | 支持 Backend 方案 | Phase 1 无原子 → Phase 2 注入 50cy 应可 opt-out |
| **Fullstack** | 先验证分位数数据 | 盲回退不可取，数据驱动决策 |

**收敛**：短期内 `#ifdef` 编译开关 opt-out（Backend 方案），长期 seqlock（Sec 方案）。

### Int32 D-143

| Agent | 方案 |
|-------|------|
| **Sec** | 简化为单条件 `(size_t)end > n`（保留核心安全语义，回补 3-5%） |
| **Backend** | 移入 `#ifndef NDEBUG`（Release 零开销，Debug/ASan 保留） |
| **Arch** | 保留（安全债务不可积累），但同意简化 |
| **Algo** | 同意 Sec 简化版 |

**收敛**：D-143 简化为单条件 `(size_t)end > n`（Sec 方案 + Backend 可选 NDEBUG 守卫），5/5 赞成。

### Int32 D-142

| Agent | 方案 |
|-------|------|
| **Arch** | **回退**（P0），收益不可辨识 + 代价已证实 |
| **Algo** | 条件编译 `#ifdef` 默认关闭 |
| **Backend** | `__builtin_expect(end-start>=8, 1)` + 条件编译 |
| **Sec** | 中性（安全无影响），听从性能 Agent |

**收敛**：D-142 条件编译 `#ifdef INT32_SEARCH_B1_SMALL_BUCKET` 默认关闭。D-130 PGO 后重评估是否启用。

### benchmark 方法增强

| Agent | 建议 |
|-------|------|
| **Fullstack** | 增加 P50/P90/P95/P99/StdDev、预热轮、禁止 Write-Progress、随机化顺序 |
| **Backend** | demo 输出每次查询原始耗时 |
| **所有人** | 一致通过 |

---

## 冲突清单

无重大冲突。所有 Agent 在核心诊断上高度一致：

| # | 分岐点 | 裁定 |
|---|--------|------|
| C-1 | D-142 回退 vs 条件编译 | 条件编译（保留验证路径），默认关闭 |
| C-2 | D-143 简化方式（Sec 单条件 vs Backend NDEBUG） | 先实施 Sec 单条件版；Backend NDEBUG 作为后续选项 |
| C-3 | Int64 方案（Backend opt-out vs Sec seqlock） | 短期 opt-out，长期 seqlock 路线图 |
| C-4 | 是否需要更多数据再决策 | Fullstack 的 bench 增强作为并行 P1，不阻塞 P0 行动 |

---

## 关键数据汇总

| 指标 | 数值 | 来源 |
|------|------|------|
| Int64 COW 原子开销 | ~44-54 cy/query | Algo + Backend |
| Int64 退化根因 | Phase 2 原子化（非 D-140 系列） | 5/5 一致 |
| D-142 回退预期回收 | Bloom OFF 50%: 7-8%, 100%: 5-6% | Algo |
| D-143 简化预期回收 | 3-5% | Sec |
| D-141 净收益 | 零开销，保留 | 5/5 一致 |
| 0.63us 尖刺根因 | lock xadd 级联 + store buffer drain + TLB miss | Backend |
| 100% 命中退化精确匹配理论 | 6-7cy / 120cy = 5-5.8% vs 实测 5.88% | Algo + Backend |
