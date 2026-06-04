---
title: 讨论记录 — Int64 扩展可行性研讨
meeting_id: meeting_012_int64_feasibility
status: Draft
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 讨论记录 — Int64 扩展可行性研讨会议

## 议题 1：Path A — AVX2 8 路 SIMD 二分 → Int64 迁移可行性

### 算法工程师（Algorithm-Engineer）

✅ **可行。** AVX2 原生提供 `_mm256_cmpgt_epi64`，语义完全对应。唯一变化是并行度从 8→4：
- block 对齐：`& ~7` → `& ~3`
- while 条件：`>= 8` → `>= 4`
- movemask：`_mm256_movemask_ps` → `_mm256_movemask_pd`，4-bit mask

性能估算：10M uniform → ~200-220 cy/query（当前 ~172 cy），退化 ~1.16-1.28x。退化远低于直觉预期的 2x，因为 log_K(N) 仅从 ~8 增加到 ~12。

**关键发现**：int64+AVX2 Path A 对标量二分（~192 cy）的加速比几乎为 1.0x。在 AVX2-only 设备上无法复现 int32 时代的 5.1x 加速比。

### 架构项目经理（Architect-Project-Manager）

✅ **可行。** intrinsic 链完整映射：`_mm256_cmpgt_epi64` + `_mm256_movemask_pd` 均已在 AVX2 中定义。`_mm256_castsi256_pd` 是零开销重解释。

算法映射完全对应，无架构障碍。二分查找是延迟绑定，不是带宽绑定——32B/step 不变，内存带宽不构成瓶颈。

### 代码安全专家（Code-Security-Expert）

✅ **可行，但有一个隐蔽高危遗漏风险。** `_mm256_movemask_ps` → `_mm256_movemask_pd` 是唯一硬性门槛。如果遗漏此变更继续使用 `_mm256_movemask_ps`，将从 64-bit lane 中间的 bit 31 读取 sign bit，导致 mask 错误、搜索结果错误（假阴性或越界）。

开发者在当前代码 [search_avx2.c:L30](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/search_avx2.c#L30) 已有注释预见此变更（"> 若修改 while 边界(如 >=4)须同步审查此处"），但未提及 movemask 变更。建议增加静态断言或强制单元测试验证 mask 宽度。

### 后端工程师（Backend-Engineer）

✅ **可行，但 search_avx2.c 是整个迁移的最高风险点。** `_mm256_cmpgt_epi64` 在 AVX2 中存在但有已知的延迟/吞吐量差异。movemask 方案需要重新设计（`_mm256_movemask_pd` 或 permute+movemask 技巧）。

### 全栈开发（Fullstack-Dev）

✅ **可行。** Path A 机械类型替换风险低、交付快。建议先跑 Path A MVP（3 天出可工作版本）。

---

## 议题 2：Path B1 — high16 目录 + lo16 扫描 → Int64 迁移可行性

### 算法工程师（Algorithm-Engineer）

⚠️ **有条件可行，但条件严苛、实用价值大幅缩水。**

B1 的两个核心优势在 int64 下同时瓦解：
1. **O(1) 桶定位**：high16 → dir[65536] = 256KB。在 int64 下 high16 仅覆盖 1/2^48 空间，桶内元素过多，退化为全表扫描。
2. **桶内 SIMD 扫描**：`_mm256_cmpeq_epi16` 16 路并行。int64 lo48 无法装入 uint16_t，即使强行截断，假阳性率无法接受。

可选方案 high20+4路扫描（4MB dir + int64 scan）在 uniform 下可行（~23 cy/query），但分布敏感性高，不再是原 B1 算法。

`interleaved 存储`（奇偶分离）在高 32 位碰撞概率近似为 0 的前提下不起筛选作用——不可行。

### 架构项目经理（Architect-Project-Manager）

⚠️ **有条件可行。** B1 当前三个不变量（high16 dir 、lo16 紧凑数组、低假阳性）在 int64 下全部打破。可行的替代：high20 dir + int64 桶内扫描。但**必须通过 POC benchmark 验证**，不可假设 int32 B1 的经验可直接迁移。

建议将 B1 的设计核心提取为"前缀目录 + 紧凑扫描"抽象模式，在 int64 下设计新 variant。

### 代码安全专家（Code-Security-Expert）

❌ **存在根本性架构阻碍。** 目录指数膨胀（16→20 位 = 16x，16→24 位 = 256x），`int32_t` 偏移在 >2.1B 数据时溢出，lo16 位宽不足以承载 int64 低 48 位。

若保留 B1，必须接受**彻底重设计**（分层目录、哈希桶等），且需要全新的安全评估。建议以"Path A only"作为 int64 MVP 首发路径。

### 后端工程师（Backend-Engineer）

✅/⚠️ **有条件可行。** high20 (8MB dir) + 保留 uint16_t lo16（二次校验保底）是推荐的工程方案。build_b1.c 可**完全不修改**（`vals[i] & 0xFFFF` 对 int64 合法）。build_decision 阈值 2000 需重新校准。

### 全栈开发（Fullstack-Dev）

⚠️ **需要独立 POC 验证。** B1 路径不能假设 int32 经验复现，必须先跑 POC benchmark 再决定是否保留。

---

## 议题 3：Bloom Filter / Scalar / API / 内存 / AVX-512

### 全员共识项

| 模块 | 结论 | 理由 |
|------|------|------|
| Bloom Filter | ✅ 可行 | xxHash 支持任意长度输入，`sizeof(key)` 自动从 4→8 |
| Scalar Search | ✅ 可行 | 二分逻辑与位宽无关，64-bit 比较指令同延迟 |
| 内存 (10M) | ✅ 可行 | vals 80MB（+40MB），带宽不影响，cache miss 次数相同 |

### API 形态争议（已解决）

| 方案 | 赞成 | 反对 |
|------|------|------|
| 独立库 libint64search | Architect, Backend | — |
| 宏泛化 | — | Architect (C11 无充分泛型支持), Backend (调试困难) |

**一致决议**：采用独立库 libint64search，复用架构模式（非代码），命名空间完全隔离。

### AVX-512 定位

- Algorithm: AVX-512 是 int64 性能转折点（8路恢复 + S-tree 可达 ~80 cy/query）
- Architect: AVX-512 是优化手段，不是可行性前提。AVX2 4 路作为基线已充分可行
- Backend: platform_memory.c 32 字节对齐暂不需升级（AVX-512 未来需 64 字节）

---

## 议题 4：综合判定

### 各 Agent 最终结论

| Agent | 结论 | 核心条件 |
|-------|------|----------|
| Algorithm-Engineer | ⚠️ 有条件可行 | 接受 AVX2 退化至 ~1.0-1.2x，或 AVX-512 为主力 |
| Code-Security-Expert | ⚠️ 有条件可行 | Path A only MVP；所有分配路径加溢出守卫 |
| Fullstack-Dev | ⚠️ 有条件可行 | Path A MVP 3天；B1 需独立 POC |
| Backend-Engineer | ⚠️ 有条件可行 | search_avx2 重写 + 阈值重校准 + XXH64 |
| Architect-Project-Manager | ⚠️ 有条件可行 | 6 项条件（C1-C6，见决议文档） |

### 一致性分析

五方一致：**⚠️ 有条件可行。** 关键共识点：

1. **Path A 完全可行**（零反对），是 int64 扩展的可信基线
2. **Path B1 是最大变数**（所有 Agent 标记为有条件/需重设计）
3. **AVX-512 是锦上添花，不是雪中送炭**
4. **独立库优于宏泛化**（一致通过）
5. **XXH32→XXH64 迁移 trivial**，Bloom Filter 零风险
