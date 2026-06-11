---
title: B1路径极限评审会 — 讨论记录
meeting_id: meeting_018_b1_limit_review
status: Reviewing
created_at: 2026-06-08
updated_at: 2026-06-08
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_018_b1_limit_review/meeting_README.md
parent_task: root
---

# B1路径极限评审会 — 讨论记录

## 参与方

| 角色 | Agent | 代号 |
|------|-------|------|
| 架构师/项目经理 | architect-project-manager | **Arch** |
| 算法工程师 | algorithm-engineer | **Algo** |
| 后端工程师 | backend-engineer | **Backend** |
| 代码安全专家 | code-security-expert | **Sec** |
| 全栈开发 | fullstack-dev | **Fullstack** |
| 主持人 | -- | **Host** |

---

## 议题1: benchmark 100轮复盘 — "略慢但更稳定"

### 各 Agent 一句话定性

| Agent | 定性 |
|-------|------|
| **Algo** | D-142 的 ~2.1cy 收益在 100 轮统计噪声（~3%）下 SNR=0.14，完全不可辨识。D-143 开销 <<1cy。稳定性来自消除小桶 SIMD 固定开销（7cy）的确定性惩罚。 |
| **Backend** | D-143 四个防御条件恒为 never-taken，分支预测正确率 ~100%，代价不可测。D-142 小桶分支 ~70% 正确预测率，整体 ROI 为正。 |
| **Arch** | "略慢"的根因是代码布局扰动（Heisenberg 效应）：D-142/D-143 注入的额外代码改变了函数体大小和 I-cache 映射，属于微架构噪声地板。 |
| **Sec** | D-143 防御覆盖完整，所有场景下阻止 OOB 读。`(size_t)end > n` 后 `end = (int32_t)n` 在 n < INT32_MAX 时无实际溢出风险。 |
| **Fullstack** | 调用方更关心 P99 而非均值。V3 方差更小对生产场景更重要。建议 bench_100.ps1 增加 P50/P90/P99 分位数输出。 |

### 共识

**"略慢"是微架构噪声，不构成退化。D-142 的 ~2cy 收益和 D-143 的 << 1cy 开销均在测量误差范围内。稳定性提升是真实的正向变化。**

---

## 议题2: Int64 B1 路径评估

### 各 Agent 意见

| Agent | 意见 |
|-------|------|
| **Algo** | 需要 D-142 等同优化，但阈值应为 **4** 而非 Int32 的 8（Int64 SIMD 每元素均摊 1.75cy vs Int32 的 0.44cy）。Int64 `vmovmskpd` 旁路延迟 ~1cy，被循环结构隐藏，无需修改。 |
| **Backend** | `vmovmskpd` 旁路延迟真实（~1cy 域交叉），但 `vpmovmskb + ctz>>3` 需要额外 ALU 指令，净收益为零。`get_bucket_key` 的 XOR+shift 已最优。 |
| **Arch** | Int64 B1 的 4 路并行度天然低于 Int32 的 16 路，SIMD-overhead-to-useful-work 比率更差。Int64 B1 优化天花板低，不建议重点投入。 |
| **Sec** | ⚠️ **Int64 B1 缺少 D-143 等效防御**。`vals + start`（start 可为负）存在 UB 风险，严重度 HIGH。必须移植。`dir[h+1]` 越界为误报（哨兵设计正确）。 |

### 共识

**Int64 B1 需要：（1）D-143 等效防御立即移植（Sec HIGH）；（2）D-142 小桶标量路径可选移植（阈值 4）；（3）结构层面无需改动。**

---

## 议题3: B1 路径剩余空间终判

### 各 Agent 一致判断

| Agent | 核心论点 |
|-------|----------|
| **Algo** | 310→90 的 220cy gap 100% 是内存子系统延迟。无纯算法手段可缩。D-140 在 `-fno-unroll-loops` 下理论收益 12-20%（SIMD 扫描部分），总查询 5-10%，建议标记 SUSPEND 等待 D-130 后重验证。 |
| **Backend** | D-140 + `-fno-unroll-loops` 在寄存器不溢出前提下是教科书级 ILP 优化。愿花 15 分钟验证。额外编译器 flag：`-march=skylake` + `-falign-loops=32` 值得一试。MinGW 上 PGO+LTO 完全可行（instrumentation-based）。 |
| **Arch** | B1 结构已达局部最优，D-144 A-F 否决无误。当前 ~60% 进度条（D-140~D-143 收尾完成），真正的战场在内存子系统（D-130~D-132，占瓶颈的 88%）。D-140 不是证伪是搁置，须在 PGO 后重评。 |
| **Sec** | 剩余优化空间几乎都是安全白嫖（无新增风险）。D-143 防御链场景 E 依赖被动保护（循环条件），建议加固。Dir fuzz 应提升到 P1。 |
| **Fullstack** | API 层协作优化（`find_with_hint`）在当前阶段 ROI 高于继续压榨 B1。B1 内存墙已至，API 层可再拿 20-50%。 |

### 唯一分歧：API 优化优先级

| Agent | 立场 |
|-------|------|
| **Fullstack** | D-135 `find_with_hint` 应从 P2 提升到 P1，与 D-130~D-133b 并行。零外部依赖，高 ROI。 |
| **Arch** | D-135 值得考虑，但 D-130~D-132（内存子系统 88% 瓶颈）应优先完成。 |

**交叉讨论**：Fullstack 论点——B1 再压榨只有 5-10%，而 `find_with_hint` 在有局部性的 workload 上可拿 20-50%。这不替代 D-130~D-132，而是同步进行。Arch 回应——同意作为并行 P1，不影响主 POC 节奏。

---

## 议题4: 项目终局建议

### 各 Agent 终局态度

| Agent | 态度 | 条件 |
|-------|------|------|
| **Algo** | 接近极限，但有明确的收尾工作 | D-130~D-133 四个 POC 完成 + D-140 SUSPEND 标记 |
| **Backend** | 支持 D-130~D-133b 后进入维护模式 | 含 D-140 快速验证 + `-march=skylake` flag 实验 |
| **Arch** | 还没到"够了"，但已看见尽头 | 三阶段收尾：POC(2-3天) → 条件验证(1-2天) → 终判(1天) |
| **Sec** | 支持进入维护模式，但有安全门禁前置条件 | SG-2（Int64 D-143）+ SG-3（dir fuzz）+ SG-4（vgather 禁令）必须闭合 |
| **Fullstack** | 支持，但有 DX 收尾条件 | D-135 原型 + CMakeLists.txt + bench 分位数 + 文档完善 |

---

## 冲突清单

| # | 冲突点 | 裁定 |
|---|--------|------|
| C-1 | D-135 `find_with_hint` 优先级：P2 → P1？ | Fullstack vs Arch 讨论后一致：提升到 P1，与 D-130~D-133b 并行 |
| C-2 | Dir fuzz 优先级：P2 → P1？ | Sec 提出，全员同意 |
| C-3 | Int64 D-142 阈值：8 还是 4？ | Algo 分析：4（Int64 SIMD 收益拐点不同），全员同意 |

---

## 关键数据汇总

| 指标 | 当前 | 资料 |
|------|------|------|
| V3 vs V1 测量差异 | < 0.5%（噪声地板内） | Algo SNR=0.14 计算 |
| D-142 均摊收益 | ~2.1cy（理论），不可测量（实际） | Algo 推导 |
| D-143 热路径开销 | << 1cy | Backend/Arch 一致 |
| D-140 理论收益（-fno-unroll-loops） | 5-10%（总查询） | Algo 12-20% SIMD 扫描部分 |
| B1 剩余空间 | 内存子系统 88%、计算 12% | meeting_017 + Arch 更新 |
| Int64 安全债务 | 1 项 HIGH（缺 D-143）+ 1 项风格不一致 | Sec 审计 |
| 维护模式最低安全门禁 | 3 项 SG-2/3/4 | Sec 提案 |
