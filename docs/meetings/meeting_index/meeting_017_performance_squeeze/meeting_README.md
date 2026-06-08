---
title: 性能压榨空间研讨会
meeting_id: meeting_017_performance_squeeze
status: Reviewing
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Executor
parent_doc: docs/meetings/meeting_index.md
parent_task: root
participants: [Architect, Algorithm, Backend, Security, Fullstack]
---

# 会议仪表盘：性能压榨空间研讨会

## 元信息
- **会议 ID**: meeting_017_performance_squeeze
- **状态**: Reviewing
- **关联任务**: root
- **日期**: 2026-06-04
- **类型**: 专题技术研讨（回答"还能不能压榨"）
- **议题源**: 人工口头提问

## 🎯 会议目标

回答单一问题:**在当前架构下(Int32 Path B1 主线 + A 回退, A+B1 已固化为 1.0.0 特性),是否还有继续压榨性能的可能性?如果有,空间多大?性价比如何?**

## 📊 现状基线(供讨论参考)

| 指标 | 数值 | 来源 |
|------|------|------|
| 10M uniform 50% hit, B1 路径 | ~470 cy/query | meeting_005 决议数据 |
| B1 路径开销分解 (meeting_016 估算) | 见下表 | D-119 论证 |
| Inline scalar (理论下限) | ~1244 cy/query (10M) | meeting_016 ACT-33 |
| Clang 14 vs GCC 11.4 (10M) | Clang +16% | meeting_016 ACT-34 |
| AVX-512 净效果 | **负收益** | meeting_016 D-119 |
| Eytzinger 排序 | **0.45x 证伪** | meeting_007 POC |
| ARM NEON | No-Go P3 | meeting_016 D-120 |
| RMI | No-Go (B1 已特化) | meeting_016 D-123 |

### B1 路径开销分解 (meeting_016 §议题4)

| 开销来源 | 估算 cycles | 占比 |
|----------|------------|------|
| Directory 查表 | ~15 | 5% |
| 桶数据内存读取 (L3/L2 miss 主导) | ~200 | **63%** |
| SIMD 比较 | ~21 | 7% |
| TLB miss / 其他 | ~82 | 25% |
| **合计** | **~318 (8M) / 470 (10M)** | **100%** |

**核心判断点:88% 时间花在内存子系统,只有 12% 在计算。**

## 📋 议程

1. **议题1**: 当前性能是否已到"内存墙"边界?还能否通过算法/工程手段压榨?
2. **议题2**: 候选优化方向的可行性与 ROI 评估
3. **议题3**: 是否还有未探索的全新方向(跳出 SIMD 二分的思路)
4. **议题4**: 投入产出判断 — 进入维护模式 vs 继续优化?

## 会议输出物

- 02_discussion.md — 各位 Agent 的意见记录
- 03_decisions.md — 决议清单
- 04_action_items.md — 行动项

## 📄 文档状态看板

| 文档名 | 状态 | 最后更新 | 来源 |
|--------|------|----------|------|
| 01_agenda.md | ✅ Frozen | 2026-06-04 | — |
| 02_discussion.md | ✅ Frozen | 2026-06-04 | 01_agenda.md |
| 03_decisions.md | ✅ Frozen | 2026-06-04 | 02_discussion.md |
| 04_action_items.md | ✅ Frozen | 2026-06-04 | 03_decisions.md |
| 05_d140_regression_audit.md | 👀 Reviewing | 2026-06-08 | D-140~D-143 性能回归审计 |

## 决议摘要

**10 项决议 D-130~D-139**,9 项通过,1 项被 Sec 安全否决(D-134 跳过原子读)。

### 核心决议
| 编号 | 内容 | 状态 |
|------|------|------|
| D-130 | PGO + LTO + 64B 对齐 立项 P1 | ✅ Go (Arch 被裁定覆写) |
| D-131 | Huge Pages POC | ✅ Go (沿用 ACT-40) |
| D-132 | 预取距离调优 | ✅ Go (条件性,需 Sec 缓解) |
| D-133a | 热键缓存 workload 埋点 | ✅ Go (P1 探针) |
| D-133b | 热键缓存完整实现 | ⏳ 条件启动 (埋点通过) |
| D-134 | 跳过原子读 | ❌ No-Go (Sec 否决) |
| D-135 | `find_with_hint` API | 🟡 P2 调研 |
| D-136 | 批量/排序批量 API | ⏸ 暂缓 |
| D-137 | 9 项伪命题归档 | ✅ Go |
| D-138 | 项目进入"定向 P1 优化"模式 | ✅ Go |
| D-139 | G1-G5 门禁评估延后,新增 G6 | ✅ Go |
| **D-140** | **2x SIMD 循环展开 (intrinsic)** | ✅ 已执行 (B1专项, 4/4) |
| **D-141** | **`_mm256_load_si256` 对齐加载** | ✅ 已执行 (B1专项, 4/4) |
| **D-142** | **小桶 (<8) 标量快速路径** | ✅ 已执行 (B1专项, 4/4) |
| **D-143** | **Sec 防御: end上界+vgather禁令** | ✅ 已执行 (B1专项, 2/2) |
| **D-144** | **B1 结构改动 A-F 全员否决** | 📦 归档 |

## 预期性能目标

| 场景 | 当前 | POC 后 | 提升 |
|------|------|--------|------|
| 10M uniform 50% (B1) | 470 cy | **320-350 cy** | 1.34-1.47x |
| 10M Zipf α=1.0 (B1) | 1560 cy | **590-690 cy** | 2.3-2.6x (条件) |
| B1 理论下界 (微架构) | -- | **~310 cy** | 1.52x (极限) |
| B1 理论下界 (+HugePages+预取) | -- | **~240 cy** | 1.96x (终极) |

## 行动项统计

| 优先级 | 总数 | 已完成 | 待执行 |
|--------|------|--------|--------|
| P1 性能 POC | 17 | 4 | 13 |
| P2 等待触发 | 3 | 0 | 3 |
| 归档 | 15 | — | — |
| **总计** | **35** | **0** | **20** |

## 项目终局判断

```
D-138: 项目进入"定向 P1 优化"模式
  ├── 4 个 POC (PGO+HugePages+Prefetch+HotKey)
  ├── 总投入 8-10 天
  ├── 预期均匀 1.3-1.4x, Zipf 条件 2.2-2.6x
  └── POC 完成后重新评估 G6 门禁 → 决定维护 OR Phase 4
```

## 冲突解决记录

| 冲突 | 裁定 | 理由 |
|------|------|------|
| C-1 PGO+LTO (Arch 放弃 vs Algo/Backend 推荐) | **采纳 Backend/Algo** | Arch 估算偏高,实际 2-3 天可落地 |
| C-2 跳过原子读 (Backend 高收益 vs Sec 高风险) | **采纳 Sec** | 收益 < 5cy, 风险 UAF |
| C-3 热键缓存立项 (Arch 条件 vs Algo 直接) | **拆为 D-133a/b** | 避免 YAGNI,先验证后投入 |

## 🔧 D-140~D-143 执行报告 (2026-06-04 执行, 2026-06-08 修订)

**状态**: ⚠️ D-140 已回退（条件编译默认关闭），D-141/D-142/D-143 保留

### 2026-06-08 修订：D-140 性能回归

人工十轮测试发现修改后代码在 Bloom OFF 50% hit 场景下有系统性的 **+25.7% 性能退化**（140ns → 176ns/q）。四位专家并行分析确认根因：

1. **GCC `-O3` 自动展开器二次展开** → YMM 寄存器溢出（Critical）
2. 分支预测器污染：命中路径从 2 个分支点膨胀到 5 个
3. D-141 对齐收益实际为零（热路径仍用 `loadu`）

**修复**：`#ifdef INT32_SEARCH_B1_UNROLL2` 包裹 D-140 默认关闭，保留 D-141/D-142/D-143，D-143 加固（新增下界检查 + `(size_t)end` 比较）。详见 [05_d140_regression_audit.md](05_d140_regression_audit.md)。

### 原始执行报告 (2026-06-04)

### 变更文件

| 文件 | 变更 | 决议 |
|------|------|------|
| `src/search_b1.c` | 2x 循环展开 + 小桶标量路径 + end 上界校验 | D-140, D-142, D-143 |
| `src/build_b1.c` | `malloc` → `platform_aligned_alloc` 32B 对齐 | D-141 |
| `src/api.c` | 4 处 `free(lo16)` → `platform_aligned_free(lo16)` | D-141 配套 |
| `Makefile` | `build_b1.o` 依赖新增 `platform_memory.h` | D-141 配套 |

### 测试结果

| 测试套件 | 项目数 | 结果 |
|----------|--------|------|
| test_b1_correctness | 6 | ✅ 0 failures |
| test_b1_boundary | 11 | ✅ 0 failures |
| test_b1_decision | 6 | ✅ 0 failures |
| test_correctness | 500K queries | ✅ 0 mismatches |
| test_boundary | 18 | ✅ 0 failures |
| test_unit | 9 | ✅ 0 failures |
| test_range | 5 + 1M | ✅ PASS |
| test_scalar_fallback | 7 | ✅ PASS |
| test_bloom | 3 + 1M | ✅ PASS |
| **总计** | **~65 + 2.5M queries** | **✅ ZERO FAILURES** |

### 编译

- GCC 15.2.0, `-O3 -std=c11 -mavx2 -Wall -Wextra`
- **零警告** — 12 个源文件全部编译通过
