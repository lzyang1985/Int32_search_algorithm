---
title: 讨论记录 — Int64 扩展 + Bloom 旁路 POC 结果评审会
meeting_id: meeting_015_poc_result_review
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 讨论记录 — Int64 扩展 + Bloom 旁路 POC 结果评审会

## 评审方式

五位专家（Architect, Algorithm, Backend, Fullstack, Security）并行独立评审 POC 报告和源码，各输出结构化评审意见。以下为各专家意见的对比与交叉分析。

---

## 1. POC 报告整体评价

| 专家 | 整体评价 |
|------|---------|
| Algorithm | POC 设计思路清晰，GATE 门槛设定合理。核心算法洞察正确：B1 加速主因是数据结构（目录+连续访存）而非 SIMD。 |
| Backend | POC 代码质量良好，benchmark 方法论严谨，三个 GATE 判定结论可信。建议批准进入 Align 阶段。 |
| Security | POC 代码安全等级可接受（Acceptable with Notes），未发现 Critical 级安全漏洞。 |
| Fullstack | 报告在数据呈现和 POC 执行完整性方面表现出色，但在 API 设计完备性方面存在显著差距。 |
| Architect | POC 执行质量总体良好，核心发现具有决定性意义。GO/NO-GO 建议方向正确。 |

**共识**：POC 结果可信，Go/No-Go 方向正确。

---

## 2. 争议议题

### 议题 A：GATE-2 FAILED 但整体仍 GO 的逻辑

| 专家 | 立场 |
|------|------|
| **Architect** (Critical) | D-106 决策树未覆盖「GATE-2 FAIL + GATE-3 PASS」路径，需要 RFC 或补充决议正式记录新决策路径。这是决策流程偏差。 |
| **Fullstack** (Critical) | 报告未显式论证为什么 GATE-2 阻塞性失败但整体方案仍可推进。隐含地将"int64 不可行"窄化为"Path A 不可行"。 |
| **Algorithm** | 方向认同。Path A 失败不代表 int64 不可行，B1 提供了替代路径。 |
| **Backend** | 确认 GATE-2 判定为 0.58x 负加速，根因正确。Path A 的 int64 AVX2 SIMD 二分确实不可行。 |
| **Security** | 无安全层面的反对意见。 |

**交叉讨论结论**：
- 5/5 同意「GATE-2 FAIL + GATE-3 PASS → B1 主线 + 标量回退 → GO」的新路径
- Architect 和 Fullstack 要求的 RFC 形式化记录获得一致支持
- 此为 **Decision D-109**

### 议题 B：B1 阈值 2000 的直接复用

| 专家 | 立场 |
|------|------|
| **Architect** (Critical) | 最薄弱的一环。meeting_010 的 crossover 数据基于 int32 B1 的特定成本模型，对 int64 完全不适用。int64 的扫描 4x 元素宽度、不同 cache miss 代价、不同的 SIMD 并行度，crossover 点可能在 500-2000 之间的任意位置。必须在立项前做 int64 crossover POC。 |
| **Algorithm** (Major) | 理论分析得出 int64 B1 收支平衡点约 1240（vs 标量二分 1560 cy/q）。阈值 2000 在平衡点之上（B=2000 时 B1 ~2510 cy/q，慢于标量 1.6x）。建议修改为每桶粒度回退而非全局回退。 |
| **Backend** (Minor) | 2000 远大于 uniform/skewed 的 max_bucket（26/71），远小于 zipf 退化值（69K/692K），区间安全。但缺少 int64 crossover 精确测量。 |
| **Fullstack** (Major) | 阈值 2000 的推导过程缺失，直接借用 int32 值。int64 桶索引空间 1048576 是 int32 的 16 倍。 |
| **Security** | 未直接涉及，但认可需要校准。 |

**交叉讨论结论**：
- 3 位专家（Architect, Algorithm, Fullstack）认为直接复用 2000 不可接受
- Algorithm 提出的收支平衡点 1240 与 Architect 建议的 crossover POC 形成互补
- 共识：Direct reuse of threshold=2000 is **NOT acceptable without int64-specific calibration**
- 此为 **Decision D-111**

### 议题 C：Phase 1 交付物重新定义

| 专家 | 立场 |
|------|------|
| **Architect** (Major) | meeting_012 D-095「Path A Only MVP」被推翻，Phase 1 需重新定义。模块清单需裁剪：删除 `search_avx2_int64.c`，改为 `search_scalar_int64.c` + `search_b1_int64.c` + `build_dir_int64.c` + `build_decision_int64.c`。 |
| **Backend** | 建议共享平台抽象层（platform_memory/platform_cpu/platform_thread），独立 int64 类型和头文件。 |
| **Algorithm** | 认同 B1 为主线 + 标量回退。建议 bucket 内二分代替全局回退（P1 改进）。 |
| **Fullstack** | API 对标清单需要补全（int32 API 有 8 个函数，报告中仅提及 3 个 int64 对应项）。 |

**交叉讨论结论**：
- 5/5 同意 Phase 1 = Path B1 主线 + Scalar Fallback
- Architect 的模块裁剪方案获一致认可
- Algorithm 的 per-bucket fallback 作为 P1 优化纳入（非 Phase 1 阻塞项）
- 此为 **Decision D-110**

### 议题 D：POC 代码质量问题

| 问题编号 | 问题 | 发现专家 | 严重度 |
|----------|------|---------|--------|
| E9 | Sign-flip `((uint64_t)key ^ (1ULL<<63)) >> 44` 在 build 和 search 中各自独立实现，未抽取为单一函数 | Backend | Major |
| E10 | POC bloom 使用自定义 MurmurHash3，非生产 XXH32/XXH64 | Backend | Major |
| SEC-POC-01 | `int32_t dir[1048577]` 在 n > 2^31 时溢出 | Security | Major |
| SEC-POC-03 | bloom_bypass memory_order 实现（acquire/release）与设计（relaxed）不一致 | Security | Minor |
| SEC-POC-04 | POC bloom 指针非原子读取（POC 安全但模式不可直接复制） | Security | Minor |
| — | 缺少 bloom_bypass getter API | Fullstack | Minor |
| — | 报告状态 Frozen 与实际存在未决决策不符 | Fullstack | Minor |

**共识**：POC 代码层面无 Critical 问题，Major 问题需在立项 DESIGN 中解决。

---

## 3. 交叉分析：各方意见对比

### 3.1 一致认同的领域

| 领域 | 共识 |
|------|------|
| GATE-1 正确性 | 5/5 确认通过 |
| GATE-2 失败根因 | 5/5 确认 SIMD 摊还效率不足 + cache miss 主导 |
| GATE-3 B1 优秀 | 5/5 确认 9.17x，方案可行 |
| GATE-4 Bloom Bypass | 5/5 确认通过 |
| Path A 降级路径 | 5/5 同意 Path A = 标量二分 |
| Bloom Bypass GO | 5/5 同意进入 Phase 1 |

### 3.2 存在分歧的领域

| 领域 | 分歧 | 决议 |
|------|------|------|
| 阈值 2000 直接复用 | 3 位要求校准 vs 1 位认为安全 vs 1 位不涉及 | D-111：需独立校准 |
| 每桶回退 vs 全局回退 | Algorithm 建议每桶粒度 | P1 优化，非 Phase 1 阻塞 |
| 立项启动时机 | 是否需先解决所有 Critical | D-112：有条件启动 |

---

## 4. 专家意见冲突交叉讨论（人工可审阅）

以下为存在观点差异的议题，经过 1 轮交叉讨论后达成一致：

### 冲突 1：阈值直接复用 vs 独立校准

- **Backend 原立场**：阈值 2000 区间安全，且远离退化值
- **Architect/Algo/Fullstack 反驳**：区间安全 ≠ 阈值最优，int64 成本模型完全不同，2000 是猜值而非校准值
- **Backend 修正**：接受。threshold 2000 作为保守初始值可接受但需标注 DEBT，建议在 Phase 1 内完成校准
- **最终共识**：D-111 — 初始阈值用保守值 256，Phase 1 内完成 int64 crossover POC 校准

### 冲突 2：全局限回退 vs 每桶回退

- **Algorithm 原立场**：每桶回退（bucket 内二分）优于全局回退，增量成本极低
- **Architect 原立场**：当前 int32 是全局决策，保持一致性有利于维护
- **Algorithm 反驳**：int64 的 zipf 退化远比 int32 严重（单桶 69% 数据），bucketed fallback 收益显著更大
- **Architect 修正**：接受每桶回退作为 Phase 1 实现目标（非单独 P1 项），因为增量复杂度极小
- **最终共识**：纳入 Phase 1 实现（不单独计行动项，融入 DESIGN）

---

## 5. 关联信息

- 前置会议：meeting_012, meeting_013, meeting_014
- 评审对象：[poc_int64_report.md](../../../decisions/poc_int64_report.md)
- POC 源码：src/poc_int64_avx2.c, src/poc_int64_b1.c, src/poc_bloom_bypass.c
