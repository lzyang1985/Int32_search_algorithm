---
title: 性能压榨空间研讨会 — 议程
meeting_id: meeting_017_performance_squeeze
status: Draft
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_017_performance_squeeze/meeting_README.md
parent_task: root
---

# 议程

## 议题 1: 当前性能是否已到"内存墙"边界?

**讨论引导问题**:
- 84MB 工作集(10M B1)在 L2(~1MB) / L3(~30MB+) 之外,理论下限在哪里?
- 每次查询实际触及多少 cache line?能否进一步压缩?
- SIMD 部分(21cy)是否还有微优化空间?

## 议题 2: 候选优化方向的 ROI 评估

**已识别候选清单**(供各位专家评估):

| 候选 | 预期收益 | 复杂度 | 状态 | 来源 |
|------|----------|--------|------|------|
| Huge Pages (2MB) | 1.45x @ 10M | 中(OS 依赖) | P1 POC ACT-40 | meeting_016 D-129 |
| 热键缓存 (Zipf) | 2.83x @ Zipf | 低(~200行) | P1 POC ACT-39 | meeting_016 D-122 |
| PGO + LTO | 5-15% | 低(纯工具链) | **未讨论** | 候选 |
| L1 64B 对齐 vals 起点 | 1-3%? | 低 | **未讨论** | 候选 |
| `__attribute__((hot))` 标注 | 1-5%? | 低 | **未讨论** | 候选 |
| BMI2 指令 (BZHI/PExt) | 不定 | 中 | **未讨论** | 候选 |
| 软件预取 (prefetch 距离调优) | 5-10%? | 中 | **未讨论** | 候选 |
| 桶内 SIMD gather 替代 dir 间接访问 | 5-10%? | 中 | **未讨论** | 候选 |
| van Emde Boas 布局 (cache-oblivious) | 1.2-1.5x? | 高 | **未讨论** | 候选 |
| 压缩指针 (vals/lo16 二选一) | 内存降,延迟不降 | 中 | **未讨论** | 候选 |
| 路径决策新增 A2 (插值搜索) | 1-1.5x (特定分布) | 中 | **未讨论** | 候选 |
| 路径决策新增 A3 (galloping) | 1-2x (局部性数据) | 中 | **未讨论** | 候选 |

## 议题 3: 跳出 SIMD 二分的新方向

**讨论引导问题**:
- 是否有未探索的算法/数据结构(跳表、B+树、Radix、Suffix Array)?
- 是否有完全不同的架构(FPGA offload / GPGPU / persistent memory / CXL)?
- 是否有调用方协作的方案(批量 API / 预热提示)?

## 议题 4: 投入产出判断

**决策框架**:
- **若总潜在空间 < 1.2x,且需 >1 周** → 直接进入维护模式
- **若总潜在空间 1.2x-1.5x** → 选 1-2 个最高 ROI 项 POC
- **若总潜在空间 > 1.5x** → 立项为 Phase 4

