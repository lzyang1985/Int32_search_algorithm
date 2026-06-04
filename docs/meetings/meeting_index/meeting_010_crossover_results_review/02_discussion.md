---
title: 讨论记录 — meeting_009 POC 执行结果评审会
meeting_id: meeting_010_crossover_results_review
status: Frozen
created_at: 2026-06-01
updated_at: 2026-06-01
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_010_crossover_results_review/meeting_README.md
parent_task: task_001_phase1_mvp
source_docs:
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/06_crossover_report.md
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/03_decisions.md
  - docs/meetings/meeting_index/meeting_009_poc_execution_plan/04_action_items.md
  - docs/requirements/总需求文档.md
tags: [review, crossover, b1, discussion]
---

# 讨论记录 — meeting_009 POC 执行结果评审会

> 四位专家（架构师、算法工程师、后端工程师、安全专家）独立完成对 [06_crossover_report.md](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/meetings/meeting_index/meeting_009_poc_execution_plan/06_crossover_report.md) 的评审。
> 以下按议题维度组织各方意见，标注共识与分歧。

---

## 议题 1：Crossover 数据可信度

### 1.1 算法工程师意见

整体评价**方法学严谨，数据可信度较高**。具体分析：

- **Crossover 点 (M≈2000) 有坚实的理论支持**：从 B 级数据拟合得到 B1 成本模型 `cost = 164.8 + 0.235×M` cy/q，标量二分 `cost ≈ 24×26.9 = 645.6` cy/q，解得 M_cross ≈ 2046，与实测 2000 吻合（偏差 < 2.3%）。
- **L3 缓存 (30.3 MiB) 起决定性作用**：B1 lo16 数组 ~19.3 MiB 全部在 L3 内，标量二分 int32 数组 ~38 MiB 超出 L3 约 8 MiB。L3 大小变化会直接改变 crossover 点。
- **"每 M 加倍，cy/q 约增加 1.4x"表述不准**：该因子从 M=5→10 的 1.01x 单调增长至 M=1000→2000 的 1.60x，不是一个常数。建议修正为"在 crossover 附近（M∈[500,2000]），M 加倍导致 B1 成本增加约 1.4-1.6x，且随 M 增大递增，极限趋于 2.0x"。
- **10M skewed 偏差 33% 不能简单归因 LCG seed**：标量二分也慢了 15%，指向系统级因素（CPU 频率/温度状态差异、跨测试会话 cache 污染）。**应重测该点**（同进程、同 seed、11 repeats）。

### 1.2 后端工程师意见

**Benchmark 方法学对 POC 级别充分可靠**，但有改进空间：

- ✅ 有效设计：6 轮轮换、2MB dummy pass、7 repeats discard 3、RDTSCP+lfence
- ⚠️ 可改进：
  - 2MB dummy pass 只刷 L2，L3（30.3MB）在跨算法轮次间可能有残留数据。建议增加到 64MB
  - 未使用 `taskset` 绑定核心，进程可能迁移
  - "中位数的中位数"统计功效低于聚合 24 个有效样本
- ⚠️ B 级 CSV 文件 (`bench_b1_crossover.csv`) 未在磁盘上找到，仅 A 级 CSV 存在

### 1.3 架构师意见

**核心交付物满足 D-074 和 D-077 标准**，但存在 3 项 Minor 偏差：

- **DEV-001**：stdout/CSV 缺少 D-074 要求的 stddev/min/max 列
- **DEV-002**：Step 2 与 Step 3 的 10M skewed 偏差 33%，归因未经验证
- **DEV-003**：crossover 阈值 "2000" 对 uniform 和 skewed 语义不同，未区分"受控构造 crossover"和"自然分布 crossover"

### 1.4 安全专家意见

从安全审计角度发现多个验证覆盖缺口（详见议题 4），但核心搜索算法的正确性（popcount 修复、lo16 边界修复、h32≥65536 防御）已在 Step 1 中通过 D-076 完整验证，crossover 代码是 Step 1 的内联复制。

### 共识

- ✅ 数据对 POC 决策目的**足够可信**
- ✅ 核心发现（B1 crossover max_bucket≈2000）**站得住脚**
- ⚠️ 10M skewed 33% 偏差需要**重测确认**
- ⚠️ 缺少 stddev/min/max 列为 Minor 偏差，不影响核心结论

---

## 议题 2：阈值 2000 的合理性

### 2.1 四位专家共识：150→2000 修正必要且紧迫

四位专家一致认为 D-015/D-019 原阈值 `max_bucket ≤ 150` 是基于 BUG-02 污染数据产生的，被 meeting_008 D-068 正确预判为"基于 buggy 数据无效"。阈值从 150 修正为 2000 是 **13.3x 的范式级偏移**，将 B1 生效窗口从 n≤1.6M 扩展到 n≤5M（uniform）乃至 n>10M（skewed）。

### 2.2 分歧：单阈值 vs 多指标决策

| 方案 | 支持者 | 理由 |
|------|--------|------|
| **单阈值 `max_bucket ≤ 2000`** | 架构师（Phase 2 v1.0） | 简单、零运行时开销、PA 回退安全 |
| **`max_bucket + weighted_avg` 双指标** | 算法工程师 | skewed 10M 场景 max_bucket=4121>2000 但实测 B1 仍有 1.33x |

**算法工程师的反例**：skewed 10M 的 max_bucket=4121 > 2000，按单阈值应选标量，但实测 B1 以 1.33x 胜出。仅用 max_bucket 会在此场景产生假阴性。

**架构师的回应**：这个 1.33x 收益属于"锦上添花"，Phase 2 v1.0 先用单阈值保证最差情况不退化（PATH_A 回退 = 172 cy/q 稳定基线）。第二决策维度可留到 Phase 3 微调。过早增加复杂度违反"最小可行"原则。

**后端工程师的折中**：建议在构建时同时计算 `weighted_avg = Σ bucket_size² / n`（一次遍历 dir 即可，O(65536) ≈ 0 开销），存入结构体但暂不使用。为将来自适应阈值积累数据。

### 2.3 跨硬件可移植性警告

**算法工程师 + 后端工程师均强调**：crossover 阈值与 L3 缓存大小强耦合。Xeon Gold 6152（L3=30.3MB）上的 M≈2000，在不同 CPU（更大/更小 L3、AVX-512）上会偏移。建议 Phase 3 考虑**运行时基准校准**或在文档中标注硬件依赖。

### 共识

- ✅ `150 → 2000` 修正**必须执行**（P0）
- ⚠️ Phase 2 v1.0 先用单阈值，Phase 3 可升级为多指标
- ⚠️ 在文档中标注阈值与 L3 缓存的耦合关系

---

## 议题 3：Pool vs 3-ptr 定位

### 3.1 性能数据分析（三位工程师共识）

Pool 始终慢于 3-ptr 5~15%（受控构造）到 10~30%（自然分布小 n）。根因经后端工程师定位为：
- **指针地址生成的流水线依赖延迟**（一条额外 LEA 指令的依赖边）
- **编译器对 `uint8_t*` → `char*` → `uint16_t*` 转换链的 aliasing 保守优化**

非 `_mm_malloc` 对齐开销或 TLB miss。

### 3.2 分歧：放弃 vs 降级 vs 保留

| 立场 | 支持者 | 理由 |
|------|--------|------|
| **放弃 Pool，只用 3-ptr** | 算法工程师、后端工程师（强建议） | POC 数据明确显示 pool 更慢，未达到 D-075 消除指针税的目标 |
| **Pool 降级为构建时内部实现细节** | 架构师 | Pool 的单次分配简化资源管理；3-ptr 作为对外接口约定。COW 也有不同权衡 |
| **保留 Pool 选项** | — | 无专家支持此立场 |

### 3.3 架构师的详细论证

- **热路径**：3-ptr 方案（`vals`, `lo16`, `dir` 三个独立 `const*`），零额外 LEA
- **构建/管理**：内部用 Pool 方式一次 `_mm_malloc`，提取三个独立指针存入 `b1_snapshot_t`
- **COW 原子性**：两个方案都需要 struct 级原子交换（D-017），Pool 不带来实质性简化

### 共识

- ✅ **热路径采用 3-ptr**（`vals`, `lo16`, `dir` 三个独立指针）
- ✅ Pool 降级为 `build_b1.c` 内部实现细节，可选使用
- ✅ `search_b1` 对外接口约定为三指针签名

---

## 议题 4：安全与规范合规

### 4.1 安全专家的发现汇总

| 编号 | 严重度 | 问题 |
|------|:------:|------|
| F-01 | **CRITICAL** | 3 处 `_mm_malloc` × 2 = 6 次调用无 NULL 检查 |
| F-02 | **HIGH** | INT32_MIN 边界键遗漏（与 D-076 不一致） |
| F-03 | **HIGH** | ASan/UBSan 未在 crossover 二进制上独立运行（无证据） |
| F-04 | MEDIUM | 缺少 n∈{0,1,5,63,64,65} 小数据量测试 |
| F-05 | MEDIUM | 缺少 M∈{65535,65536,65537} 边界邻域测试 |
| F-06 | MEDIUM | `max_bucket≤2000` 跨硬件不可移植 |
| F-07 | LOW | `(int32_t)((uint32_t)i)` 实现定义行为（n>2.1B 触发） |

### 4.2 其他专家对安全发现的反馈

**后端工程师** 确认 F-01 属实，补充：
- 15 GiB 内存服务器 102MB 峰值下不易触发，但代码健壮性不足
- `platform_aligned_malloc` 已在项目中存在，可复用
- 栈 `temp_dir[65537]`（256KB）在 POC 中安全，但生产需改为堆分配（macOS 辅助线程仅 512KB 栈）

**架构师** 对签收条件的判定：
- F-01~F-03 属于**补充验证层面**而非**逻辑缺陷**，不影响核心结论
- 核心搜索算法正确性已在 Step 1 中通过 D-076 验证，Step 3 是内联复制
- **建议**：完成安全专家提出的 SV-01~SV-05 补充验证后即可签收

### 共识

- ✅ F-01（NULL 检查）**必须在签收前修复**
- ✅ F-02（INT32_MIN）**必须在签收前补充**
- ✅ F-03（ASan 独立运行）**必须在签收前补充证据**
- ⚠️ F-04~F-07 可标注 `[DEBT]`，不阻塞签收

---

## 议题 5：Phase 2 启动条件

### 5.1 四位专家一致意见

**meeting_009 POC 执行已达到设计目标** — 修复 bug 后重新校准 B1 crossover 点。数据可信，方向明确。

### 5.2 架构师提出的前置条件清单

| 编号 | 条件 | 优先级 | 说明 |
|:--:|------|:------:|------|
| PC-01 | 更新总需求文档 §6.3（`150→2000`） | **P0** | 验收标准必须与实际一致 |
| PC-02 | 完成安全 SV-01~SV-05 补充验证 | **P0** | 签收安全门控 |
| PC-03 | 重测 10M skewed 消除 33% 偏差 | **P1** | 确认噪声 vs 代码差异 |
| PC-04 | 创建 RFC 记录阈值变更 | **P1** | 需求变更追溯 |
| PC-05 | 记录 DEV-001~003 偏差到 ACCEPTANCE | **P1** | 交付物完整性 |

### 5.3 对总需求文档 §6.3 的超限评估

原验收标准"1.5M 均匀数据自动选中 B1（max\_bucket <= 150）"，但实测 1.5M uniform 的 max\_bucket=824。按旧阈值应判定为 PATH\_A，但实测 B1 以 2.50x 胜出。**旧标准已与实测矛盾，必须修正。**

### 共识

- ✅ **Phase 2 v1.0 可以启动**，条件为 PC-01~PC-05 完成
- ✅ PC-01/PC-02 为硬性门控（P0），不完成不启动
- ✅ PC-03~PC-05 可与 Phase 2 实施并行

---

## 交叉讨论与最终立场

### 完全一致（4/4）

1. 阈值 `150→2000` 修正必须立即执行
2. 热路径采用 3-ptr 方案
3. meeting_009 数据对 POC 目的足够可信
4. 安全 SV-01~SV-05 补充验证为签收前置条件
5. Phase 2 v1.0 可以启动（满足前置条件后）

### 存在分级共识（2~3/4，可接受）

1. **决策函数复杂度**：架构师 + 后端主张 Phase 2 v1.0 单阈值；算法主张加入 `weighted_avg` 但承认可延后。→ **结论**：Phase 2 v1.0 单阈值，Phase 3 升级。
2. **Pool 最终命运**：算法 + 后端主张放弃；架构师主张降级为内部细节。→ **结论**：降级（不暴露到热路径接口），复用 3-ptr。

### 无分歧

1. 无任何专家对 crossover 报告的核心结论（"B1 crossover at max_bucket≈2000"）提出异议
2. 无任何专家建议推迟 Phase 2 v1.0 启动
