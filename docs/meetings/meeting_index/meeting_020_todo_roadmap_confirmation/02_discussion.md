---
title: 会议讨论记录 — 剩余待办事项收尾路线确认会
meeting_id: meeting_020_todo_roadmap_confirmation
status: Reviewing
created_at: 2026-06-10
updated_at: 2026-06-10
author: Agent_Executor
parent_doc: docs/meetings/meeting_index.md
source_docs:
  - "docs/meetings/meeting_index/meeting_016_optimization_direction/"
  - "docs/meetings/meeting_index/meeting_017_performance_squeeze/"
  - "docs/meetings/meeting_index/meeting_018_b1_limit_review/"
  - "docs/meetings/meeting_index/meeting_019_benchmark_regression_review/"
  - "docs/tasks/task_006_int64_phase2_cow/"
participants: [Architect, Fullstack, Algorithm, Sec, Backend]
---

# 讨论记录

## 1. 议题一：Demo V1-V4 现状与性能结论

### 1.1 Demo 文件清单
- `demo_search.c` — Int32 Search (default, ~50% hit)
- `demo_int64_search.c` — Int64 Search (~50% hit)
- `demo_int32_no_bloom.c` — Int32 Search (Bloom OFF, ~50% hit)
- `demo_int32_no_bloom_fullhit.c` — Int32 Search (Bloom OFF, 100% hit)

### 1.2 V1 vs V4 差异分析
- V4 曾包含 D-140（2x SIMD展开）和 D-142（小桶标量路径），均通过条件编译宏控制
- meeting_019 后：D-140 和 D-142 均默认关闭（条件编译 `#ifdef`），D-141（32B对齐）和 D-143-minimal（防御检查）已合入基线
- 结论：**V4 概念在 meeting_019 后已自然消解**，V1 和 V4 用默认编译参数编译后完全一致

### 1.3 bench_100 数据（10遍×100次）
- Int32 default: V4 vs V1 ±1-3%（噪声地板内）
- Int64: V4 vs V1 -4~+7%（含最终遍 +7.58% faster）
- Bloom OFF 50%: V4 vs V1 ±1-4%
- Bloom OFF 100%: V4 vs V1 ±1-6%
- 结论：**全部四场景均在噪声地板内，meeting_019 的 P0 修复已验证成功**

---

## 2. 议题二：28项行动项去重与分类

### 2.1 去重结果
Architect 指出"28项"存在严重跨会议重复计数。去重后实际唯一行动项 **16 项**：

| U-ID | 内容 | 出现位置 |
|------|------|----------|
| U-01 | Int64 B1 D-143 等效防御移植 | m018 ACT-41 = m019 ACT-58 |
| U-02 | PGO+LTO+64B对齐 | m017 ACT-41~44 = m018 ACT-42 |
| U-03 | Huge Pages POC | m016 ACT-40 = m017 ACT-45~47 = m018 ACT-43 |
| U-04 | 预取距离调优 | m017 ACT-48~50 = m018 ACT-44 |
| U-05 | 热键缓存埋点 | m017 ACT-51~53 = m018 ACT-49 |
| U-06 | Int64 D-142 小桶标量移植 | m018 ACT-45 |
| U-07 | Dir fuzz 基础覆盖率 | m018 ACT-46 |
| U-08 | find_with_hint 最小原型 | m018 ACT-47 |
| U-09 | 编译器flag实验 | m018 ACT-48 |
| U-10 | bench_100.ps1 方法论增强 | m019 ACT-57 |
| U-11 | D-140 重验证（PGO后） | m018 ACT-50 |
| U-12 | 热键缓存完整实现 | m018 ACT-51 |
| U-13 | DX收尾 | m018 ACT-52 |
| U-14 | Int64 COW seqlock调研 | m019 ACT-59 |
| U-15 | find_with_hint API调研 | m017 ACT-55（已被U-08覆盖） |
| U-16 | 批量查询API | m017 ACT-56 |

### 2.2 专家组分类共识

**A类（必须完成，安全/正确性/可维护性门禁）**：U-01, U-07, U-10 — 5/5 全票同意

**B类（值得做，ROI可论证）**：U-02(PGO部分), U-03, U-06 — 共识存在但范围有分歧

**C类（锦上添花，条件触发）**：U-05, U-08, U-13 — 存在优先级分歧

**D类（建议关闭）**：U-04, U-09, U-11, U-12, U-14, U-15, U-16 — 多数或全票同意关闭/降级

---

## 3. 交叉讨论：四个核心冲突

### 3.1 冲突 C-1：PGO+LTO 集成 (U-02)

**正方（Architect）**：编译器基础设施投资，二进制质量提升，D-130已被Host裁定覆写
**反方（Backend）**：静态库项目，PGO训练数据无法代表用户workload，LTO改变库发布格式

**交叉裁决（Architect中立第三方）**：
- **PGO 保留**（1天），作为 D-140/D-142 重评估的前置平台
- **LTO opt-in**（0.5天），显示 `make lto` 目标，不做默认路径
- **64B 对齐立即独立**（5min），零风险零成本
- 合计 1.5 天（非原 2-3 天）

### 3.2 冲突 C-2：find_with_hint 最小原型 (U-08)

**正方（Fullstack）**：API协作ROI高，有局部性场景20-50%收益，退化路径零代价
**反方（Architect）**：无业务需求信号，API面扩大带来维护负担

**交叉裁决（Fullstack中立第三方）**：
- **维持 P1**，附加 benchmark 验收门
- 验收条件：随机 workload 无退化（>1%），有局部性场景有可测量收益
- 文档标记 `@experimental`

### 3.3 冲突 C-3：预取距离调优 (U-04)

**正方（Algorithm）**：7-12% ROI，攻击B1 pointer chasing核心路径
**反方（Architect）**：收益递减区，安全复杂度高（SEC-G3）

**交叉裁决（Algorithm中立第三方）**：
- **分阶段执行，Gate 条件硬性**
- Stage 0（0.5天）：L3 miss rate baseline，miss < 5% → 直接关闭
- Stage 1（1天）：仅 Gate 通过后触发，最优配置收益 < 5% → 关闭
- Stage 2：安全评估（如 Stage 1 通过）

### 3.4 冲突 C-4：D-140 重验证 (U-11)

**正方（Algorithm）**：30min时间盒，理论上无spill，信息价值高
**反方（Architect）**：与编译器博弈，长期维护成本不可接受

**交叉裁决（Backend中立第三方）**：
- **准予执行，30min时间盒，二元结局**
- 收益 ≥ +3% → 合并（附加 CI 回归门禁）
- 收益 < +3% 或回归 > +2% → 永久归档，删除条件编译块
- **不再保留 SUSPEND 中间态**

---

## 4. 安全门禁状态

| 门禁 | 内容 | 状态 |
|------|------|------|
| SG-1 | ASan/UBSan/TSan 零告警 | ✅ PASS |
| SG-2 | Int64 B1 D-143 等效防御检查 | ❌ FAIL — 需 U-01 修复 |
| SG-3 | Dir fuzz 基础覆盖率 (Int32+Int64) | ❌ FAIL — 需 U-07 补充 |
| SG-4 | vgather 禁令文档化 | ⚠️ PARTIAL |
| SG-5 | -fstack-protector-strong | DEFER |
| SG-6 | Cache 侧信道评估 | DEFER |

**维护模式当前不通过。阻塞项：SG-2 + SG-3，合计约 1.5 天。**

---

## 5. Demo V1/V4 处置共识

- V4 概念自然消解，bench_100.ps1 移除 V4 对比逻辑
- 改为单版本 N 次统计（支持 `-VariantSuffix` 灵活对比实验性编译）
- 4 个 demo 源文件保留（覆盖矩阵合理）
- 10.ps1 继续工作但更快（不再跑 V4）
