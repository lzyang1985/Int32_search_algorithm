---
title: 决议记录 — Int64 扩展 + Bloom 旁路 POC 结果评审会
meeting_id: meeting_015_poc_result_review
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
human_confirmation: "2026-06-02 确认接受 D-109 新 Go/No-Go 路径"
final_confirmation: "POC 审查通过。B1 主线 + Scalar Fallback + Bloom Bypass。人工确认 D-109 新 Go/No-Go 路径后启动立项。"
---

# 决议记录 — Int64 扩展 + Bloom 旁路 POC 结果评审会

## 📋 决议总览

| 决议编号 | 议题 | 结论 | 票数 |
|----------|------|------|------|
| D-108 | POC 报告整体评审 | ✅ 报告可信，Go/No-Go 方向正确 | 5/5 |
| D-109 | GATE-2 FAIL + GATE-3 PASS 新 Go 路径 | ✅ 正式确认新路径：Path A=标量 + B1 主线 → GO | 5/5 |
| D-110 | Phase 1 交付物重定义 | ✅ Path B1 主线 + Scalar Fallback + Bloom Bypass | 5/5 |
| D-111 | B1 阈值 int64 校准 | ✅ 不直接复用 int32=2000，初始保守值 256 + Phase 1 内 crossover POC 校准 | 5/5 |
| D-112 | 立项启动条件 | ⚠️ 有条件启动：需人工确认 D-109 + 完成 int64 crossover POC 后进入 Align | 5/5 |
| D-113 | Critical/Major 问题处置 | ✅ 分类处置（见 D-113 详细） | 5/5 |
| D-114 | POC 报告修正 | ✅ 更新 poc_int64_report.md：补充缺失章节 + 标注 DEBT + 状态 Reviewing | 5/5 |

---

## D-108：POC 报告整体评审

**结论：✅ 报告可信，Go/No-Go 方向正确（5/5）**

**理由**：
1. POC 执行方法论严谨：6 轮旋转消除 cache 效应、三路对比（B1/AVX2/Scalar）、多分布 benchmark
2. GATE 验证覆盖全面：正确性（GATE-1）、性能（GATE-2/3）、并发安全（GATE-4）
3. ASan/UBSan 零告警，五份交叉评审（Algorithm/Backend/Security/Fullstack/Architect）均认可数据可信度
4. 根因分析方向正确，四位专家（除 Security 不涉及此评估外）确认

**保留意见**：
- Fullstack：报告在 API 设计完备性方面不足，但 POC 阶段可接受
- Architect：阈值 2000 直接复用 int32 值缺乏论证
- Algorithm：Path A 失败根因分析缺少 cache line crossing 和 MLP 阻塞两条

---

## D-109：新 Go/No-Go 路径正式确认

**结论：✅ 正式确认「GATE-2 FAIL + GATE-3 PASS → GO」新路径（5/5）**

**原决策树（meeting_014 D-106）局限性**：
```
全部通过 → GO
仅 Path A → GO (B1 推迟)
任何阻塞性 GATE 失败 → NO-GO / BLOCKED
```

**新路径**：
```
GATE-1 ✅ + GATE-2 ❌ + GATE-3 ✅ + GATE-4 ✅
  → int64 扩展可行（Path A 降为标量二分 + Path B1 主线）
```

**理由**：
1. GATE-2 原始定义「AVX2 SIMD 二分 ≥ 1.2x」的失败仅证伪 Path A 的 SIMD 方法，不证伪 int64 扩展本身
2. GATE-3 的 9.17x 超预期通过（远超 1.5x 门槛）证明了 B1 路径在 int64 下的可行性甚至优于 int32 的相对表现
3. meeting_012 D-090 的「不可行条件」涵盖 AVX-512 替代方案，B1 作为第三条路本质上是更优替代
4. 标量二分作为回退有成熟稳定实现，无技术风险

**风险**：低。新路径仅调整了对 GATE-2 失败含义的解读，不改变任何已验证的技术结论。

**⚠️ 需要人工签收**：此决议绕过了 meeting_014 D-106 决策树中「阻塞性 GATE 不通过 → 整体 NO-GO」的原始定义，属于解释性调整而非技术决策。需人工确认。

---

## D-110：Phase 1 交付物重定义

**结论：✅ Path B1 主线 + Scalar Fallback + Bloom Bypass（5/5）**

**修正后的 Phase 1 模块清单**：

```
libint64search Phase 1:

Layer 2: 构建与选路层
├── build_sorted_int64.c      — 模式复制 build_sorted.c
├── build_dir_int64.c         — NEW: high20 目录构建（int64_t dir 类型待定）
└── build_decision_int64.c    — NEW: 独立阈值决策（初始保守值 256）

Layer 3: 查询引擎层
├── search_scalar_int64.c     — 模式复制 search_scalar.c（int64 标量二分）
└── search_b1_int64.c         — NEW: high20 + lo44 4路 cmpeq 扫描 + 每桶回退

Layer 4: API 层
├── api_int64.c               — 模式复制 api.c
├── internal_int64.h          — 独立 int64_search_impl_t
└── int64_search.h            — 独立公开头文件

扩展层:
├── bloom_filter.c/h          — 复用（XXH64 升级待 P2）
└── bloom_bypass              — 模式复制 _Atomic(int) bloom_bypass
```

**删除的模块**（相对于 meeting_012 D-091 原计划）：
- `search_avx2_int64.c` — AVX2 4 路 SIMD 二分已被证伪，不创建

**保留的待确认事项**：
| 事项 | 说明 |
|------|------|
| dir 类型 | 当前 `int32_t`。若需支持 n > 2^31，改为 `int64_t`（8MB 目录） |
| 每桶回退 | 纳入 Phase 1 实现（Architect 修改后在交叉讨论中确认） |
| API 对标 | `destroy/rebuild/version/find_range/config_t/get_bloom_bypass` 需补全 |

**影响范围**：独立库 libint64search，不影响现有 libint32search。

---

## D-111：B1 阈值 int64 独立校准

**结论：✅ 不直接复用 int32=2000，初始保守值 256 + Phase 1 内校准（5/5）**

**不直接复用的理由**（由 Algorithm + Architect 联合论证）：
1. int32 B1 成本模型 `cost = 164.8 + 0.235×M` 对 int64 完全不适用
2. 关键差异：
   - 桶内元素宽度 8 byte vs 2 byte → 内存带宽 4x
   - SIMD 并行度 4 路 vs 16 路 → 吞吐 4x
   - 二次校验 `vals[pos]==target` 在 int64 B1 中必须执行（latency cost ~5 cy/hit）
3. Algorithm 理论分析得出 int64 B1 收支平衡点约 1240，int32=2000 在 1240 之上 1.6x，存在误杀风险
4. Architect 指出 meeting_010 crossover 数据针对 int32，直接引用是方法论错误

**处置方案**：
| 阶段 | 行动 | 阈值 |
|------|------|------|
| 立项 DESIGN | 使用保守初始值 | **256** |
| Phase 1 实现 | 实现 `build_decision_int64()` 可配置阈值 | 256（可覆盖） |
| Phase 1 内 | 执行 int64 B1 crossover POC（受控 M 序列 8~8192） | 校准为实测值 |
| Phase 1 验收 | 以校准后阈值为准 | 实测 crossover 点 |

**保守值 256 的选择依据**：
- uniform 1M max_bucket=8 ≪ 256
- skewed 10M max_bucket=71 ≪ 256
- uniform 10M max_bucket=26 ≪ 256
- zipf 1M max_bucket=69,732 ≫ 256（回退触发✅）
- zipf 10M max_bucket=692,681 ≫ 256（回退触发✅）
- 安全余量：256/26 ≈ 10x 对 uniform 10M

---

## D-112：立项启动条件

**结论：⚠️ 有条件启动（5/5）**

**前置条件**：
| # | 条件 | 负责 | 优先级 |
|---|------|------|--------|
| 1 | 人工确认 D-109 新 Go/No-Go 路径 | 人类 | **阻塞性** |
| 2 | POC 报告更新（D-114） | Agent | **阻塞性** |
| 3 | int64 B1 crossover POC | Agent | 可在 Align 阶段并行 |

**立项流程**：
```
人工确认 D-109
  ↓
POC 报告更新（D-114）
  ↓
Align 阶段启动（ALIGNMENT_int64_b1.md）
  │  └── 并行: int64 B1 crossover POC
  ↓
Architect 阶段（DESIGN_int64_b1.md）
  │  └── 阈值校准结果写入 DESIGN
  ↓
Atomize 阶段（TASK_int64_b1.md）
```

---

## D-113：Critical/Major 问题分类处置

**结论：✅ 分类处置 — 立项前解决 / 立项 ADT 阶段解决 / 推迟（5/5）**

### 立项前必须解决（均在 POC 报告更新 D-114 中处理）

| 编号 | 问题 | 严重度 | 来源 |
|------|------|--------|------|
| 1 | GATE-2 FAIL 与整体 GO 逻辑桥接 | Critical | Architect, Fullstack |
| 2 | 报告状态 Frozen → Reviewing | Minor | Fullstack |
| 3 | 补充 API 对标清单 | Major | Fullstack |
| 4 | 补充缺失设计决策项（5 项） | Major | Fullstack |
| 5 | 补充安全评审章节 | Minor | Security |
| 6 | memory_order 设计偏差记录 | Minor | Security, Backend |

### 立项 Align/Design 阶段解决

| 编号 | 问题 | 严重度 | 来源 |
|------|------|--------|------|
| E9 | Sign-flip 抽取为单一内联函数 | Major | Backend |
| E10 | Bloom 参数对齐（POC vs 生产） | Major | Backend |
| SEC-POC-01 | dir int32_t 溢出（n > 2^31） | Major | Security |
| — | int64 COW 原子交换方案设计 | Major | Architect |
| — | API 完整对标设计 | Major | Fullstack |
| — | bloom_bypass getter 添加 | Minor | Fullstack |

### Phase 1 内解决

| 编号 | 问题 | 严重度 | 来源 |
|------|------|--------|------|
| — | int64 B1 crossover POC 阈值校准 | Critical | Architect, Algorithm |
| — | 每桶回退实现 | Major | Algorithm |

### Phase 2/3 推迟

| 编号 | 问题 | 严重度 | 来源 |
|------|------|--------|------|
| — | Eytzinger int64 POC benchmark | Minor | Algorithm |
| — | AVX-512 8 路探索 | Minor | Architect |
| — | SEC-B1/B2/B3（meeting_013 已知并发问题） | 中高危 | Security |

---

## D-114：POC 报告修正

**结论：✅ 更新 poc_int64_report.md（5/5）**

**修正项**：
1. **状态修正**：`status: Frozen` → `status: Reviewing`（存在未决决策）
2. **新增 §1.1 "GATE-2 新路径解释"**：说明 GATE-2 失败仅证伪 Path A SIMD 路线，不证伪 int64 扩展
3. **新增 §3.4 "安全评审摘要"**：记录 Security 评审的主要发现
4. **修正 §3.2 B1 阈值**：`建议 2000` → `初始保守值 256，Phase 1 内 int64 crossover POC 校准`
5. **新增 §5.1 "完整 API 对标清单"**：列出 int32 API 8 个函数与 int64 对应项
6. **新增 §5.2 "缺失设计决策补充"**：5 项缺失决策
7. **标注 DEBT 项**：
   - `[DEBT] B1 阈值待 int64 crossover POC 校准（初始保守值 256）`
   - `[DEBT] COW 原子交换方案待立项 DESIGN 设计`
   - `[DEBT] dir int32_t 容量上限 2B 条，超出需改为 int64_t`

---

## 📊 决议执行优先级

| 优先级 | 决议 | 行动 |
|--------|------|------|
| P0 | D-109 | 人工确认新 Go/No-Go 路径 |
| P0 | D-114 | 更新 POC 报告 |
| P0 | D-111 | 启动 int64 B1 crossover POC |
| P0 | D-112 | 条件满足后启动立项 Align 阶段 |
| P1 | D-110 | Phase 1 模块清单写入 DESIGN |
| P1 | D-113 | Major 问题在 DESIGN 中解决 |
| P2 | D-113 | Phase 2/3 推迟项记录到技术路线 |

---

## 关联信息

- 源会议：meeting_015_poc_result_review
- 前置会议：meeting_012, meeting_013, meeting_014
- POC 报告：[poc_int64_report.md](../../../decisions/poc_int64_report.md)
- 技术路线：[技术路线.md](../../../architecture/技术路线.md)
- 总需求：[总需求文档.md](../../../requirements/总需求文档.md)
