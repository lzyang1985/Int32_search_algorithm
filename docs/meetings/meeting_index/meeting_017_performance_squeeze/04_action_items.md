---
title: 性能压榨空间研讨会 — 行动项
meeting_id: meeting_017_performance_squeeze
status: Reviewing
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_017_performance_squeeze/meeting_README.md
parent_task: root
---

# 性能压榨空间研讨会 — 行动项

## 优先级说明

| 级别 | 含义 | 时限 |
|------|------|------|
| P1 | 本周启动,核心 POC | 7-10 天 |
| P2 | 等待触发条件 | 不限 |
| 归档 | 不执行 | — |

---

## P1 行动项(本周启动)

| # | 行动项 | 类型 | 工作量 | 主导 Agent | 来源决议 | 状态 |
|---|--------|------|--------|------------|----------|------|
| **ACT-41** | PGO 三阶段集成: Makefile 加 `prof-gen` / `prof-use` target,主 build 加 `-flto` | 配置 | 1-2 天 | Backend | D-130 | ⬜ 待执行 |
| **ACT-42** | 64B 对齐 vals 起点: `platform_memory.c` 改 `aligned_alloc(64, ...)` | 配置 | 0.5 天 | Backend | D-130 | ⬜ 待执行 |
| **ACT-43** | PGO 收益验证: 跑 10M uniform 50% benchmark,验收 ≤ 420 cy | 测试 | 0.5 天 | Backend | D-130 | ⬜ 待执行 |
| **ACT-44** | LTO+PGO 集成 CI: Linux runner 加 8GB 内存下限,跑 PGO 三阶段 | 配置 | 1 天 | Backend | D-130 | ⬜ 待执行 |
| **ACT-45** | Huge Pages POC: `perf stat -e dTLB-load-misses` baseline | 调研 | 0.5 天 | Algo | D-131 | ⬜ 待执行 |
| **ACT-46** | Huge Pages POC: `platform_memory.c` 实现 2MB 对齐 + `madvise(MADV_HUGEPAGE)` | POC | 1 天 | Algo | D-131 | ⬜ 待执行 |
| **ACT-47** | Huge Pages POC: benchmark 对比 4KB vs 2MB 页面 | POC | 0.5 天 | Algo | D-131 | ⬜ 待执行 |
| **ACT-48** | 预取距离 baseline: `perf stat -e cache-misses,cache-references` | 调研 | 0.5 天 | Algo | D-132 | ⬜ 待执行 |
| **ACT-49** | 预取距离调优: `__builtin_prefetch` 3-5 组 distance 实验 | POC | 1 天 | Algo | D-132 | ⬜ 待执行 |
| **ACT-50** | 预取调优验证: `callgrind` 验证 L3 命中率改善 | 测试 | 0.5 天 | Algo | D-132 | ⬜ 待执行 |
| **ACT-51** | 热键缓存埋点: `benchmark/bench_data_gen.c` 加 zipf 分布生成 | 调研 | 0.5 天 | Algo | D-133a | ⬜ 待执行 |
| **ACT-52** | 热键缓存埋点: `search_b1.c` 入口埋点 + 跑 1M queries | 调研 | 1 天 | Algo | D-133a | ⬜ 待执行 |
| **ACT-53** | 热键缓存埋点: 输出 `top-1%` / `top-5%` / `top-10%` 命中率报告 | 调研 | 0.5 天 | Algo | D-133a | ⬜ 待执行 |

### 新增 B1 热路径 P1 (2026-06-04 补充)

| # | 行动项 | 类型 | 工作量 | 主导 Agent | 来源决议 | 状态 |
|---|--------|------|--------|------------|----------|------|
| **ACT-57** | 2x SIMD 循环展开: `search_b1.c` 热循环改为 2x 展开 | 实现 | 0.5 天 | Backend | D-140 | ✅ 完成 |
| **ACT-58** | lo16 32B 对齐: `build_b1.c` 改 `platform_aligned_alloc(n*sizeof(uint16_t), 32)` + `search_b1.c` 改用 `_mm256_load_si256` | 实现 | 0.5 天 | Backend | D-141 | ✅ 完成 |
| **ACT-59** | 小桶标量路径: `search_b1.c` 入口加 `if (end-start < 8)` 标量快速路径 | 实现 | 0.5 天 | Algo | D-142 | ✅ 完成 |
| **ACT-60** | Sec 防御: `search_b1.c` 加 `if (end > (int32_t)n) end = (int32_t)n` + 文档标注 vgather 禁令 | 安全 | 0.5 天 | Sec | D-143 | ✅ 完成 |

---

## P2 行动项(等待触发)

| # | 行动项 | 触发条件 | 来源决议 |
|---|--------|----------|----------|
| **ACT-54** | 热键缓存完整实现 (256-1024 项 direct-mapped,~200行) | D-133a 验证 top-1% > 40% | D-133b |
| **ACT-55** | `find_with_hint` API 接口设计 + 调研 | 业务场景需求 (流量表/时序数据) | D-135 |
| **ACT-56** | 批量查询 API 调研 (find_batch / find_sorted_keys) | 决策树批量推理 / RocksDB MultiGet 类需求 | D-136 |

---

## 安全门禁(全行动项必须满足)

| # | 要求 | 来源 |
|---|------|------|
| **SEC-G1** | TSan 全量复测零告警 | Sec 通用要求 |
| **SEC-G2** | ASan/UBSan 复测零告警(LTO 死代码消除防御) | Sec 通用要求 |
| **SEC-G3** | 越界预取守卫 `if (i + stride < n) _mm_prefetch(...)` | D-132 |
| **SEC-G4** | 埋点代码静态开关 `#ifdef INT32_SEARCH_PROFILE_KEYS` | D-133a |
| **SEC-G5** | 热键缓存绑定 `rebuild_gen` epoch 字段 | D-133b |
| **SEC-G6** | PGO profile 文件不入版本控制 | D-130 |

---

## 归档 (不执行)

| # | 方向 | 标注 |
|---|------|------|
| — | 跳过原子读 | `[DEBT-SkipAtomic] 收益 < 5cy, 风险 UAF` |
| — | van Emde Boas 布局 | `[DEBT-vEB] 均匀随机访问反模式` |
| — | 3-byte 编码 | `[DEBT-Compression] 瓶颈延迟非带宽` |
| — | 跳表 / B+ / Radix | `[DEBT-NewStructures] 静态排序集无优势` |
| — | 手写汇编 | `[DEBT-Asm] 维护成本压垮收益` |
| — | `__attribute__((flatten))` | `[DEBT-Flatten] 二进制膨胀 2-3×` |
| — | BMI2 BZHI/PExt | `[DEBT-BMI2] 已被 SIMD 覆盖,边际收益` |
| — | branchless 终态 | `[DEBT-Branchless] GCC -O3 自动做` |
| — | 批量/排序批量 API | `[YAGNI-BatchAPI] 等待业务需求` |

---

## 统计

| 优先级 | 总数 | 预计工作量 |
|--------|------|------------|
| P1 性能 POC | 13 + 4 = **17 项** | ~10-12 天 | 4 已完成 |
| P2 调研/等待 | 3 项 | 触发后评估 | 0 |
| 归档 | 9 + 6 = **15 项** | — | — |
| **总计** | **35 项** | — | **4 已完成** |

---

## 时间表

```
Week 1 (本周):
  ├── ACT-41 PGO 三阶段 Makefile 集成
  ├── ACT-42 64B 对齐 (0.5 天,可并行)
  ├── ACT-45/46/47 Huge Pages POC
  ├── ACT-48/49/50 预取调优
  └── ACT-57/58/59/60 B1 热路径微优化 (2 天,与上述并行)

Week 2 (下周):
  ├── ACT-43 PGO 收益验证
  ├── ACT-44 LTO+PGO CI 集成
  ├── ACT-51/52/53 热键缓存埋点
  └── (条件性) ACT-54 热键缓存完整实现

Week 3+:
  ├── 全 POC 汇总 → 性能目标验收
  ├── 重新评估 G6 门禁
  └── 决议: 进入维护模式 OR 启动 Phase 4
```

---

## 验收清单(POC 完成后必须全部打勾)

- [ ] ACT-43: 10M uniform 50% ≤ 420 cy (5% 提升)
- [ ] ACT-47: 10M uniform 50% Huge Pages 提升 1.45x (从 470 → ~325 cy)
- [ ] ACT-50: 10M uniform 50% 预取优化后 L3 命中率 ≥ 85%
- [ ] ACT-53: 热键缓存埋点报告输出
- [ ] SEC-G1: TSan 全绿
- [ ] SEC-G2: ASan/UBSan 全绿
- [ ] SEC-G3~G6: 各项安全门禁
- [ ] D-140: B1 2x SIMD 展开后 benchmark 验证 ≥ 3% 提升
- [ ] D-141: B1 对齐 load 验证 `_mm256_load_si256` 不触发 SIGBUS
- [ ] D-142: 小桶标量路径正确性 (1M 交叉验证)
- [ ] D-143: `end > n` 防御 + vgather 禁令文档
- [ ] FINAL: 项目总结报告 + TODO 列表
