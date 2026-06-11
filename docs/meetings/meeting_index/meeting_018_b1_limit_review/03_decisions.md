---
title: B1路径极限评审会 — 决议
meeting_id: meeting_018_b1_limit_review
status: Reviewing
created_at: 2026-06-08
updated_at: 2026-06-08
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_018_b1_limit_review/meeting_README.md
parent_task: root
---

# B1路径极限评审会 — 决议

## 投票统计

| 决议 | 内容 | Arch | Algo | Backend | Sec | Fullstack | 结果 |
|------|------|:---:|:---:|:---:|:---:|:---:|:---:|
| **D-145** | V3 "略慢但更稳定" 定性为微架构噪声，非退化 | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |
| **D-146** | B1 路径剩余空间终判：算法/指令级已耗尽，仅内存子系统有空间 | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |
| **D-147** | D-140 标记 SUSPEND，待 D-130 PGO 后重验证 | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |
| **D-148** | Int64 B1 移植 D-143 等效防御（立即 P0） | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |
| **D-149** | Int64 B1 移植 D-142 小桶标量路径（阈值 4，P1） | ✅ | ✅ | ✅ | ✅ | -- | **4/4 通过**, 🔒 |
| **D-150** | D-135 `find_with_hint` 从 P2 提升到 P1 | ✅ | -- | -- | -- | ✅ | **2/2 通过**, 🔒 |
| **D-151** | Dir fuzz 从 P2 提升到 P1 | ✅ | -- | -- | ✅ | -- | **2/2 通过**, 🔒 |
| **D-152** | 维护模式安全门禁：SG-2+SG-3+SG-4 为最低准入条件 | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |
| **D-153** | 项目路线图：D-130~D-133b POC 收尾 → G6 评估 → 维护模式 | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |

---

## 每条决议的详细说明

### D-145: V3 "略慢但更稳定" 定性

**核心结论**：
- V3 "最好成绩略慢" 是**微架构代码布局扰动**（Heisenberg 效应），不构成退化
- D-142 均摊收益 ~2.1cy（0.45%），低于 100 轮统计噪声地板（~3%），不可测量
- D-143 开销 << 1cy（never-taken 分支零惩罚）
- V3 "更稳定" 来自消除小桶 SIMD 固定开销（7cy）的确定性惩罚

**数值支撑**：
- D-142 在小桶（桶<8，30%查询量）上节省 ~7cy 固定开销
- 加权收益 = 0.30 × 7cy ≈ 2.1cy = 0.45%（相对 ~470cy 基线）
- 100 轮统计噪声 σ≈1-3%，SNR=0.45/3=0.14 << 1

**行动**：接受 V3 为当前基线。不进一步追求 D-142 的可测量性验证。

---

### D-146: B1 路径剩余空间终判

**核心结论**：B1 路径的算法/指令级优化空间已耗尽。

**已耗尽的方向**：
- SIMD 指令调度（D-140~D-142 已探索）
- 分支优化（D-143 never-taken 已最优）
- 数据布局（D-144 A-F 全员否决，结构已达局部最优）
- 编译器指令选择（Backend 确认 `vmovmskpd` 为最优）

**仍有空间的方向（但不在算法层）**：
- 内存子系统：D-130 PGO+LTO（5-15%）、D-131 Huge Pages（-60~70cy）、D-132 预取调优（-30~50cy）
- 应用层协作：D-135 `find_with_hint`（20-50%，有局部性场景）
- 操作系统/编译器：`-march=skylake`、`-falign-loops=32`、PGO profiling

**理论下界确认**：
```
当前实测：  ~470 cy  (10M uniform 50% hit)
微架构下界： ~310 cy  (D-130~D-132 完成后)
终极下界：  ~240 cy  (含 HugePages+预取)
纯算法下界：  ~90 cy  (L1 resident, 物理不可达)
```

---

### D-147: D-140 标记 SUSPEND

**历史回顾**：
- D-140（2x SIMD 循环展开）在 meeting_017 决策：4/4 通过
- 2026-06-04 执行，正确性测试全部通过
- 2026-06-08 人工发现 +25.7% 回归（Bloom OFF 50% hit），根因：GCC `-O3` 自动展开器二次展开 → YMM 寄存器溢出
- 已用 `#ifdef INT32_SEARCH_B1_UNROLL2` 条件编译包裹，默认关闭

**当前判断**：
- D-140 不是在理论上被否决的（不同于 D-144/D-137），而是被不匹配的编译选项打败的
- 在 `-fno-unroll-loops` 下，5 个 YMM 寄存器（key+c0+c1+e0+e1）远低于 Skylake 的 16 个 YMM 预算，寄存器溢出概率极低
- 理论收益：SIMD 扫描部分 12-20%，总查询 5-10%

**决议**：
- 标记为 `[SUSPEND-D-140] 等待 D-130 PGO 完成后在 Linux + -fno-unroll-loops 下重验证`
- 不归档（非伪命题），不立项（条件不成熟），不默认启用
- 验证方案：PGO 基线 vs PGO + D-140 + `-fno-unroll-loops`，用 bench_100 对比

---

### D-148: Int64 B1 D-143 等效防御（P0 立即）

**严重度：HIGH**

**根因**：`search_b1_int64.c` 中 `search_int64_scalar(vals + start, ...)` ，如果 dir 损坏导致 start 为负，`vals + (-1)` 是 UB。

**修复方案**（3 行）：

```c
/* 在 get_bucket_key 和 start/end 赋值之后立即加入 */
if (start < 0 || end < 0 || start >= end)
    return (size_t)-1;
if ((size_t)end > n) end = (int32_t)n;
if (start >= end) return (size_t)-1;  /* end 被 clamp 后需重检 */
```

**执行**：立即实施，不等待会议 Frozen。

---

### D-149: Int64 B1 D-142 小桶标量路径移植（P1）

**阈值分析**（Algo 推导）：

| 桶大小 | SIMD 路径 | 纯标量 | 优胜方 |
|--------|----------|--------|--------|
| 1 | 3 (broadcast) + 3 (tail) = 6cy | 3cy | 标量 |
| 2 | 3 + 6 = 9cy | 6cy | 标量 |
| 3 | 3 + 9 = 12cy | 9cy | 标量 |
| 4 | 7 (4路一次) = 7cy | 12cy | SIMD |

**阈值 = 4**（Int32 为 8，因为 Int32 SIMD 16 路并行密度高得多）

**预期收益**：均摊 ~1cy（低于测量噪声），主要价值在方差削减。低风险，2 行改动。

---

### D-150: D-135 `find_with_hint` 从 P2 提升到 P1

**原状态**：meeting_017 D-135 → P2 调研，"等待具体业务需求场景"

**重新评估理由**（Fullstack 提出，Arch 同意）：
1. B1 路径算法层已到极限，库本身只能再挤 5-10%
2. API 层协作优化（hint）在有局部性场景下可拿 20-50%
3. 零外部依赖，纯 C 代码，2 天原型
4. 不阻塞 D-130~D-133b POC（并行推进）

**实现范围**：
- 最小原型验证（2 天）
- 先做 B1 路径 hint（分支点最明确）
- 实验性头文件标记 `INT32_SEARCH_EXPERIMENTAL`

**验收**：
- hint 命中率 benchmark（时序数据/局部性 workload）
- 对比无 hint 的 B1 路径延迟
- 决定是否正式纳入 API

---

### D-151: Dir fuzz 从 P2 提升到 P1

**原状态**：meeting_017 D-143 §3 → P2，"1 天，补充 fuzz target"

**重新评估理由**（Sec 提出，Arch 同意）：
1. D-143 防御链在场景 E 依赖循环条件的"被动保护"而非显式检查
2. Int64 B1 当前无任何防御，fuzz 可一并覆盖
3. 投入 1 天，低风险，高防御价值

**实现范围**：
- 注入 dir 值对（start, end）到 search_b1_find，验证无 crash/OOB
- 覆盖范围：start∈[-128, 2048], end∈[-128, 2048]
- 同时覆盖 Int32 和 Int64 B1（移植 D-148 后）

---

### D-152: 维护模式安全门禁

**最低准入条件（Sec 提议，全员通过）**：

| 门禁 | 内容 | 优先级 |
|------|------|--------|
| SG-1 | ASan/UBSan/TSan 零告警（所有 target） | 必须 |
| SG-2 | Int64 B1 D-143 等效防御检查 | 必须（D-148） |
| SG-3 | Dir fuzz 基础覆盖率（Int32 + Int64） | 必须（D-151） |
| SG-4 | vgather 禁令文档化（CVE-2022-40982） | 必须 |
| SG-5 | 编译时安全加固选项（`-fstack-protector-strong`） | 建议（可推迟） |
| SG-6 | Cache 侧信道评估（D-132 预留） | 可推迟 |

---

### D-153: 项目路线图调整

**终局路线图**（取代 meeting_017 D-138/D-139 的原始时间线）：

```
Phase A: 收尾 POC (1-2 周)
  ├── D-148 Int64 D-143 (SEC-P0, 立即)        ← 🆕
  ├── D-130 PGO+LTO (Backend, 2-3 天)
  ├── D-131 Huge Pages (Algo, 1-2h Linux)
  ├── D-132 预取调优 (Algo, 1-2 天)
  ├── D-151 Dir fuzz (Sec, 1 天)               ← 🆕 从 P2 提升
  ├── D-150 find_with_hint 原型 (Fullstack, 2天) ← 🆕 从 P2 提升
  └── D-149 Int64 D-142 移植 (P1, 30min)       ← 🆕

Phase B: 条件验证 (1 周)
  ├── D-133a 热键埋点 → 判定 D-133b 触发条件
  ├── D-147 D-140 重验证 (PGO + -fno-unroll-loops)
  └── Backend: -march=skylake + -falign-loops=32 实验

Phase C: 终判 (1-2 天)
  ├── bench_100 终测（所有 POC 产物 vs baseline）
  ├── G6 门禁评估（含 D-152 SG-1~SG-4 安全闭合）
  └── 决策: 维护模式 OR 继续投入

Gating Criteria:
  ├── 若 PGO+HugePages+Prefetch 总收益 < 10% → 维护模式
  ├── 若收益 10-20% → 将 D-133b/D-140 作为 P2 保留
  └── 若收益 > 20% → D-140 正式化
```

**新增门禁（G6 扩展）**：
```
G6 = G1 + G2 + G3 + G4 + G5 + (D-130~D-133b 完成)
     + D-148 (Int64 D-143) + SG1~SG-4 (安全闭合)
```

---

## 决议路线图

```
✅ 立即 (D-148)
  └── Int64 B1 D-143 等效防御 (SEC-P0)

📅 本周 (D-130 ~ D-132 推进 + D-149/D-151)
  ├── D-130 PGO + LTO (Backend 主导, 2-3 天)
  ├── D-131 Huge Pages POC (Algo 主导, 1-2h Linux)
  ├── D-132 预取调优 (Algo 主导, 1-2 天)
  ├── D-149 Int64 D-142 移植 (P1, 30min)
  ├── D-151 Dir fuzz (Sec, 1 天)
  └── D-150 find_with_hint 原型 (Fullstack, 2天)

⏳ 条件性
  ├── D-147 D-140 重验证 (PGO + -fno-unroll-loops, P2)
  └── D-133b 热键缓存 (仅当 D-133a 验证通过)

📅 终判
  └── bench_100 全量终测 → G6 门禁评估 → 维护模式
```
