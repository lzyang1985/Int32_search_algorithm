---
title: 性能压榨空间研讨会 — 讨论记录
meeting_id: meeting_017_performance_squeeze
status: Reviewing
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_017_performance_squeeze/meeting_README.md
parent_task: root
---

# 性能压榨空间研讨会 — 讨论记录

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

## 议题1: 整体判断 — 还能压榨吗?

### 各 Agent 一句话定性

| Agent | 定性 |
|-------|------|
| **Arch** | 还能压,但天花板清晰可见,窄门非蓝海 |
| **Algo** | 理论下界 50-80cy,当前 470cy,真金白银约 100-150cy (470→320) |
| **Backend** | PGO + LTO + 对齐是低垂果实,手写汇编和 `flatten` 是陷阱 |
| **Sec** | 大部分可白嫖,但 3 条必须明确规避(详见议题2冲突) |
| **Fullstack** | 调用方协作能拿 10-40%,不需改库 |

### 决策框架套用(Arch 提供)

```
候选总潜在空间(均匀50%场景,综合所有候选):
├── Huge Pages:        1.45x ✅
├── PGO + LTO:         1.10-1.15x ✅
├── 预取调优:          1.07-1.10x ✅
├── 64B 对齐 + hot:    1.02-1.05x ✅
├── 软件预取安全化:    0 (无新增)
├── 跳过原子读:        0 (被 Sec 否决,见议题2)
├── 压缩编码 / vEB:    < 1.0x (伪命题)
├── 批量 API:          见 Fullstack 议题
│
├── 总乐观上限(均匀):   1.4-1.5x (叠加边际递减)
└── 总乐观上限(Zipf):   2.83x (热键缓存命中时)
```

**结论**: 均匀场景卡在 **1.2-1.5x 区间**,符合 **"选 1-2 个最高 ROI POC"** 档位,**无需立项 Phase 4**。

Zipf 场景下热键缓存可作为 **Phase 4 候补**(条件立项,需 workload 验证)。

---

## 议题2: 候选方向评审(冲突识别)

### ✅ 高 ROI 真东西(全员基本一致)

| 方向 | Arch | Algo | Backend | Sec | 共识 |
|------|:---:|:---:|:---:|:---:|------|
| **A1 Huge Pages** | ✅ P1 选 1 | ✅ Top-1 -60~70cy | -- | 低风险 | **Go** (D-131) |
| **C1 PGO + LTO** | (放弃,理由见下) | ✅ Top-3 -20~40cy | ✅ Top-1, 5-15% | 低风险 | **Go** (D-130) |
| **C2 预取调优** | -- | ✅ Top-2 -30~50cy | ✅ 中等 | 中风险 | **Go** (D-132) |
| **A4 64B 对齐** | -- | 边际 | ✅ Top-3, 1-3% | 低风险 | **Go** (D-130 附带) |
| **B4 热键缓存** | ✅ 条件立项 | ✅ Top, 2.83x | -- | 中风险 | **条件 Go** (D-133) |

### ⚠️ 冲突1: PGO + LTO 是否值得?

| Agent | 立场 | 理由 |
|-------|------|------|
| **Arch** | ❌ 放弃 | "叠加不超过 1.2x 且投入超 1 周 → 落维护" |
| **Algo** | ✅ 推荐 | "PGO 让 dir_lookup 内联并紧贴 caller,消除 call/ret (5-10cy)" |
| **Backend** | ✅✅ 强烈推荐 | "单次最大收益点,同行数据 jemalloc 5-8%, RocksDB 8-12%, nginx 3-5%" |

**讨论要点**:
- Arch 的"投入 > 1 周"是按"全套 PGO 改造"估算;Backend 实测认为基础 PGO+LTO 集成 **2-3 天可完成**(Makefile 加 target + 跑 workload + 链接产物)
- Backend 引用同行数据: LTO 2-5%, PGO 5-12%, **组合往往 ≫ 单独之和**
- Sec 确认 LTO/PGO 是低风险(纯整数 profile 无 PII)

**Host 裁定**: 采纳 Backend + Algo 意见。**PGO+LTO ROI 实际 ~10-15%,投入 2-3 天**,在 1.2-1.5x 区间内是"必做"而非"放弃"。Arch 估算偏高。

### ⚠️ 冲突2: 跳过原子读 (Backend Top-5 vs Sec 高风险)

| Agent | 立场 | 理由 |
|-------|------|------|
| **Backend** | ✅ Top-5 | "热路径上原子读是 10× 慢" |
| **Sec** | ❌ 高风险 | "直接破坏 acquire/release 内存序,Int64 rebuild UAF 历史会重演" |

**讨论要点**:
- Backend 目标是优化 `reader_count` 的 atomic 读
- Sec 反驳: reader 路径可以 `relaxed`,但 rebuild 标志必须 `release/acquire`,**禁止裸读**
- 实际收益: 一次 atomic 读 vs 普通读差 ~1-2ns,每次查询 1-2 次 atomic 读,总节省 < 5cy
- **性价比极低**(< 1%),但风险等级 HIGH

**Host 裁定**: 采纳 Sec 意见, **不做**。 标注为 `[DEBT-SkipAtomic]` 关闭,原因: 收益 < 5cy, 风险 UAF, ROI 极差。

### ⚠️ 冲突3: 热键缓存是否值得条件立项?

| Agent | 立场 |
|-------|------|
| **Arch** | ✅ 条件立项, **需 workload 验证 > 40% top-1% 命中率** 再投入完整实现 |
| **Algo** | ✅ 强推,256 项 hash cache 期望 2.83x |
| **Sec** | ⚠️ 中风险,需 epoch 绑定 rebuild_gen,版本失配立即降级主表 |
| **Fullstack** | -- (未直接评估) |

**讨论要点**:
- 2.83x 仅在 Zipf α=1.0 成立,通用场景可能 < 1.1x
- Arch 建议分两步: **1) 1-2 天埋点** → **2) 验证后再投入完整实现**
- 避免 YAGNI: 如果实际 workload 不集中,降级为研究储备

**Host 裁定**: 采纳 Arch 方案, **D-133 拆为 D-133a(埋点验证,2 天) + D-133b(实现,1-2 天,条件性)**。验证失败 → 直接归档。

### ❌ 伪命题(全员否决)

| 方向 | 否决理由 |
|------|----------|
| **A2 van Emde Boas 布局** | 对**均匀随机**访问是反模式(Algo 2分) |
| **A3 3-byte 编码** | 瓶颈是延迟不是带宽,解压进关键路径 +5-10cy(Algo 2分) |
| **D1-3 跳表/B+/Radix** | 静态 10M 排序集点查,几乎不可能击败"dir+vals" 两次访存(Algo 2分) |
| **手写汇编** | GCC 已能输出几乎相同代码,维护成本压垮收益(Backend) |
| **`__attribute__((flatten))`** | 二进制膨胀 2-3×,冷函数被错误内联进热路径(Backend) |

### ⚠️ 边际项(< 5%, 不做)

| 方向 | 理由 |
|------|------|
| BMI2 BZHI/PExt | 已被 SIMD 覆盖,边际收益 |
| branchless 终态 | GCC -O3 自动做 |
| `likely/unlikely` | PGO 跑完后 GCC 自动学 |
| `always_inline` | 重复,容易 bloat |

---

## 议题3: 全新方向 — 跳出 SIMD 二分

### 各 Agent 意见

| 方向 | Arch | Algo | Backend | Sec | Fullstack |
|------|:---:|:---:|:---:|:---:|:---:|
| 跳表 | -- | ❌ 静态排序集无优势 | -- | -- | -- |
| B+ 树 | -- | ❌ 节点分裂开销 | -- | -- | -- |
| Radix | -- | ❌ 10M 整数基数树常数项大 | -- | -- | -- |
| GPGPU/FPGA | -- | ❌ 偏离定位 | -- | -- | -- |
| CXL/PMem | -- | ❌ 项目规模不需要 | -- | -- | -- |

### Fullstack 提议:调用方协作 API

| API | 预期收益 | Host 判断 |
|-----|----------|-----------|
| `find_with_hint` | 20-50% (流量表/时序数据) | ✅ P2 调研 (D-135) |
| `find_batch` (N=16) | 1.5-2.5x 批量决策树 | ⚠️ 需明确业务场景 (D-136) |
| `find_sorted_keys` | O(n+k) 替代 O(n log k) | ⚠️ 调用方场景有限 (D-136) |
| `stats` / `set_budget` | -- | ❌ 误用率高,80% 业务不会读 stats |

**Fullstack 的关键观察**: "杀手级调用模式"包括
- 业务循环里 `find(a); find(b); find(c);` 改成 batch → 30%+
- 命中后不重置 hint,每次从头二分
- output 缓冲区每次 `malloc/free`

调用方侧优化**真实贡献 10-40%**,不需改库。

**Host 判断**: 调用方协作属"另一个赛道", 不应塞进 Phase 4 立项, 但 `find_with_hint` 是高 ROI 项。决议见 D-135。

---

## 议题4: 投入产出判断 — 维护模式 vs 继续优化

### Arch 的提案(决策框架)

| 档位 | 标准 | 当前判断 |
|------|------|----------|
| < 1.2x, > 1 周 | 直接维护 | ❌ 否决 (PGO+LTO 投入 2-3 天) |
| 1.2x-1.5x | 选 1-2 个 POC | ✅ **是** (Huge Pages + PGO+LTO) |
| > 1.5x | 立项 Phase 4 | ❌ 不需要 (Zipf 单独候补) |

### 全员共识

- **项目进入"定向 P1 优化"模式**, 不是简单进入维护模式
- 4 个 POC 投入(总 ~6-8 天):
  1. PGO + LTO + 64B 对齐 (2-3 天) → D-130
  2. Huge Pages POC (1-2 天) → D-131
  3. 预取距离调优 (1-2 天) → D-132
  4. 热键缓存 workload 埋点 (2 天) → D-133a
- 总预期收益: 均匀 50% 场景 **1.3-1.4x (470→330-360 cy)**
- Zipf 场景若埋点验证通过: **2.5x+ (1560→600 cy)**
- 4 项 POC 全部跑完后再做 G1-G5 维护门禁评估

### Sec 的额外建议

- 任何优化合并前必须 TSan 全量复测
- 优化合并后必须用 ASan/UBSan 复测,防止 LTO 死代码消除误删安全检查
- PGO profile 文件不入版本控制(避免污染)

---

## 冲突清单

| # | 冲突点 | 裁定 |
|---|--------|------|
| C-1 | PGO+LTO 是否值得(Arch 放弃 vs Algo/Backend 推荐) | **采纳 Backend/Algo,Go** |
| C-2 | 跳过原子读(Backend 高收益 vs Sec 高风险) | **采纳 Sec,No-Go** |
| C-3 | 热键缓存立项(Arch 条件 vs Algo 直接) | **拆为 D-133a/b,条件 Go** |

---

## 关键数据汇总(供下游决议引用)

| 指标 | 当前 | 优化后目标 | 来源 |
|------|------|-----------|------|
| 均匀 50% B1 路径 | 470 cy | **330-360 cy** | Algo + Backend 估算 |
| Zipf α=1.0 B1 路径 | 1560 cy | **600 cy** (条件) | Algo 热键缓存估算 |
| TLB miss 开销 | 82 cy | ~10 cy | Algo Huge Pages 估算 |
| 内存读开销 | 200 cy | 130-150 cy | Algo 预取估算 |
| 理论下界 | -- | **50-80 cy** | Algo 给出 |

