---
title: V3 性能回归紧急评审 — 行动项
meeting_id: meeting_019_benchmark_regression_review
status: Frozen
created_at: 2026-06-09
updated_at: 2026-06-09
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_019_benchmark_regression_review/meeting_README.md
parent_task: root
---

# V3 性能回归紧急评审会 — 行动项

## 行动项总览

| 优先级 | 总数 | 新建 | 已完成 | 待执行 |
|--------|------|------|--------|--------|
| P0 立即修复 | 4 | ACT-53 ~ ACT-56 | 4 ✅ | 0 |
| P1 本周 | 2 | ACT-57 ~ ACT-58 | 0 | 2 |
| P2 条件/调研 | 1 | ACT-59 | 0 | 1 |

---

## P0 — 立即修复（今天完成）

### ACT-53: D-156 Int64 单线程 opt-out 编译开关 ✅ 已完成

| 字段 | 内容 |
|------|------|
| **决议** | D-156 |
| **负责人** | Backend |
| **状态** | ✅ **2026-06-09 完成** |
| **实际产出** | `src/internal_int64.h`: `#ifdef INT64_SEARCH_MULTI_THREAD` 双路径结构体。`src/api_int64.c`: `find`/`create`/`destroy`/`rebuild`/`bloom_bypass` 全部 `#ifdef` 双路径。GCC 15.2.0 `-O3 -Wall -Wextra` 零警告。 |
| **验证** | `make test-int64` ~50 项全部通过。12遍 bench_100.ps1 确认 Int64 退化消失。 |

---

### ACT-54: D-157 D-142 条件编译默认关闭 ✅ 已完成

| 字段 | 内容 |
|------|------|
| **决议** | D-157 |
| **负责人** | Algo |
| **状态** | ✅ **2026-06-09 完成** |
| **实际产出** | `src/search_b1.c`: D-142 小桶 <8 标量路径用 `#ifdef INT32_SEARCH_B1_SMALL_BUCKET` 包裹默认关闭。 |
| **验证** | `make test-b1` 18 项全部通过。12遍 bench_100.ps1 确认 Bloom OFF 50% 退化消失。 |

---

### ACT-55: D-158 D-143 防御检查最小化 ✅ 已完成

| 字段 | 内容 |
|------|------|
| **决议** | D-158 |
| **负责人** | Sec |
| **状态** | ✅ **2026-06-09 完成** |
| **实际产出** | `src/search_b1.c`: D-143 从 4 条件/2 分支缩减为单条件 `(size_t)end > n`。原版 `#ifdef INT32_SEARCH_B1_FULL_DEFENSE` 保留。 |
| **验证** | `make test-b1` 全部通过，安全语义不变。 |

---

### ACT-56: @Host 干净环境重测 ✅ 已完成

| 字段 | 内容 |
|------|------|
| **决议** | D-154 |
| **负责人** | @Host |
| **状态** | ✅ **2026-06-09~10 完成** |
| **实际产出** | @Host 在干净环境（屏幕关闭 + 无人值守）下完成 12 遍 bench_100.ps1 全量测试。前 2 遍有人操作干扰，后 10 遍完全无人值守。 |
| **验证结果** | 四场景退化全部消失，V4 与 V1 持平（±3% 噪声地板）。Int64 最快 +7.58% faster, Bloom OFF 50% 最快 +2.62% faster。详见 [05_verification.md](05_verification.md)。 |

---

## P1 — 本周完成

### ACT-57: D-161 bench_100.ps1 方法论增强

| 字段 | 内容 |
|------|------|
| **决议** | D-161 |
| **负责人** | Fullstack |
| **描述** | 1. 新增 P50/P90/P95/P99 和标准差输出<br>2. 前 5 轮预热丢弃<br>3. 关闭 Write-Progress<br>4. V1/V3 随机交错执行<br>5. 可选：记录 CPU 频率和环境快照 |
| **文件** | `demo/bench_100.ps1` |
| **验收** | 新脚本输出分位数信息，预热和数据采集分离 |
| **预计时间** | 1 h |

---

### ACT-58: Int64 D-143 等效防御移植（meeting_018 ACT-41 继续）

| 字段 | 内容 |
|------|------|
| **决议** | meeting_018 D-148 |
| **负责人** | Sec |
| **描述** | 将 D-143 最小化单条件版移植到 `search_b1_int64.c`。注意 Int64 的 `dir` 类型为 `int32_t`（非 `uint16_t`），但上界检查逻辑相同。 |
| **文件** | `src/search_b1_int64.c` |
| **验收** | `make clean-int64 && make lib-int64 && make test-int64` 通过 |
| **预计时间** | 30 min |

---

## P2 — 条件启动

### ACT-59: D-162 Int64 COW seqlock 调研

| 字段 | 内容 |
|------|------|
| **决议** | D-162 |
| **负责人** | Backend + Sec |
| **描述** | 调研 seqlock（序列锁）替代方案：<br>1. 安全性证明（与 COW acq_rel 协议等价性）<br>2. 性能建模（预期 reader ~25cy vs 当前 ~50cy）<br>3. 实施路线图<br>仅在 P0 行动全部完成且 @Host 签收后启动。 |
| **触发条件** | D-156 opt-out 方案被 @Host 签收 + 多线程需求浮现 |
| **验收** | 技术可行性报告 |
| **预计时间** | 2-3 天 |
