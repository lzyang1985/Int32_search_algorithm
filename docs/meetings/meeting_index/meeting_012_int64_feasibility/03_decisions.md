---
title: 决议记录 — Int64 扩展可行性研讨
meeting_id: meeting_012_int64_feasibility
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
final_confirmation: "扩展到 Int64 可行，同时支持 Int32 与 Int64"
---

# 决议记录 — Int64 扩展可行性研讨会议

## 📋 决议总览

| 决议编号 | 议题 | 结论 | 票数 |
|----------|------|------|------|
| D-090 | Int64 扩展总体可行性 | ⚠️ 有条件可行 | 5/5 |
| D-091 | Path A AVX2 迁移 | ✅ 可行 | 5/5 |
| D-092 | Path B1 架构模式复用 | ⚠️ 需独立 POC 验证 | 5/5 |
| D-093 | API 形态为独立库 | 采用 libint64search | 5/5 |
| D-094 | AVX-512 定位 | 优化手段，非可行性前提 | 5/5 |
| D-095 | Path A only MVP 首发策略 | 建议采纳 | 5/5 |
| D-096 | Bloom Filter/Scalar 迁移 | ✅ 直接可行 | 5/5 |

---

## D-090：Int64 扩展总体可行性

**结论：⚠️ 有条件可行**

五位 Agent 一致判定：从 Int32 → Int64 扩展存在明确可行的技术路径，但受到以下条件约束：

### 核心条件（总数量：6 项）

| 编号 | 条件 | 类型 | 详情 |
|------|------|------|------|
| C1 | Path A SIMD intrinsic 功能验证 | 阻塞性 | 确认 `_mm256_cmpgt_epi64` + `_mm256_movemask_pd` 在 GCC ≥ 8.0 上正确，1000+ 随机测试交叉验证 |
| C2 | Path B1 独立 POC benchmark | 阻塞性 | 1M/5M/10M uniform + skewed + zipf 多分布 benchmark，不可假设 int32 经验迁移 |
| C3 | 独立库决议 | 设计决策 | 采用 libint64search 独立库，非宏泛化，命名空间完全隔离 |
| C4 | AVX-512 预留 | 设计预留 | `INT64_SEARCH_AVX512` 编译宏预留，但不阻塞 Phase 1 交付 |
| C5 | 性能基线 | 质量门控 | 10M uniform 下 AVX2 4 路 vs 标量二分实测，加速比预期 ~4x |
| C6 | 架构复用 | 工程约束 | 四层架构 + COW + 不透明句柄模式直接复用 |

### 不可行的情况

如果以下约束不成立，则 int64 扩展**在当前硬件前提下不可行**：

> 在 AVX2-only 设备上，int64 Path A 对标量二分的加速比低于 1.2x——这意味着用户投入 int64 库的回报可能不显著。只有当用户的目标平台包含 AVX-512 或接受"在 AVX2 上性能基本持平标量"时，int64 库才具有实用价值。

---

## D-091：Path A AVX2 迁移可行性

**结论：✅ 可行（5/5）**

技术基础坚实，无原则性障碍：

```c
// 当前 int32 (8路):
_mm256_set1_epi32 → _mm256_loadu_si256 → _mm256_cmpgt_epi32 → _mm256_movemask_ps

// 迁移 int64 (4路):
_mm256_set1_epi64x → _mm256_loadu_si256 → _mm256_cmpgt_epi64 → _mm256_movemask_pd
```

关键参数变更：
- block 对齐：`mid & ~7` → `mid & ~3`
- while 条件：`hi - lo >= 8` → `hi - lo >= 4`
- le_count：`8 - popcount(mask)` → `4 - popcount(mask)`
- 标量尾部逻辑不变

⚠️ **高危注意**：`_mm256_movemask_ps` → `_mm256_movemask_pd` 不可遗漏，否则产生隐蔽错误。

---

## D-092：Path B1 架构模式复用

**结论：⚠️ 需独立 POC 验证（5/5）**

B1 当前三个核心不变量在 int64 下均被打破：

| 不变量 | int32 状态 | int64 状态 |
|--------|-----------|-----------|
| high16 目录 O(1) 定位 | dir[65536], 256KB | high16 覆盖不足，退化为全表扫描 |
| lo16 紧凑数组 (2B/元素) | 16 路 `_mm256_cmpeq_epi16` 并行 | lo48 无法装入 uint16_t |
| 低假阳性率 (<1%) | 可接受 | 若硬截 low16，假阳性极高 |

**可行替代**：high20 dir (8MB) + lo44 int64 bucket scan (4路并行)，配合二次校验（`vals[pos]==target`）。

**POC 验证要求**（纳入 C2）：1M/5M/10M 多分布 benchmark，与 Path A 对比。

**降级方案**：如 B1 POC 未达预期，接受"int64 库仅 Path A + 标量回退"的简化架构。

---

## D-093：API 形态决议

**结论：采用独立库 libint64search（5/5）**

| 理由 | 说明 |
|------|------|
| C11 兼容性 | `_Generic` 无法全面支持函数指针类型泛化 |
| 类型安全 | 编译期完全隔离 int32_t / int64_t |
| B1 路径差异 | int64 B1 需重新设计，与 int32 版本不可共享 |
| 维护成本 | 两个独立但模式相同的库 >> 单一宏泛化库 |
| 用户体验 | `int64_search_create()` 命名清晰，按需链接 |
| 命名空间 | 内部符号加 `int64_` 前缀，避免链接冲突 |

**代码复用策略**："模式复制"（pattern replication），非"宏泛化"（macro generalization）。

---

## D-094：AVX-512 定位

**结论：优化手段，非可行性前提（5/5）**

```
AVX-512 (8路 int64, 512/64=8) → 恢复与 int32+AVX2 同等并行度
AVX2   (4路 int64, 256/64=4) → 基线，CPU 覆盖广泛
Scalar (1路)                  → 回退，绝对正确性基准
```

- AVX2 4 路作为基线已充分可行，AVX-512 不阻塞 int64 Phase 1 交付
- 技术路线预留 `INT64_SEARCH_AVX512` 编译宏（参考现有 D-021 策略）
- AVX-512 S-tree 方案可达 ~80 cy/query，作为 Phase 2/3 高级特性评估

---

## D-095：首发策略

**结论：Path A Only MVP（5/5）**

```
Phase 1 首发: 
  Path A (AVX2 4路二分) + Scalar 回退 + Bloom Filter + COW
  → 最小可行交付，验证核心技术栈

Phase 2 评估:
  基于 B1 POC benchmark 结果决定是否纳入 B1 路径

Phase 3 扩展:
  AVX-512 8路 + S-tree 优化（可选）
```

---

## D-096：Bloom Filter / Scalar / 内存

**结论：✅ 直接可行（5/5）**

| 组件 | 状态 | 备注 |
|------|------|------|
| Scalar Search | ✅ 零修改逻辑 | 比较运算符对 int64 通用 |
| Bloom Filter | ✅ `sizeof(key)` 4→8 | xxHash 支持变长，建议 XXH32→XXH64 |
| 内存 (10M) | ✅ 80MB vals | 带宽不影响，cache miss 次数相同 |
| COW 原子交换 | ✅ x86-64 lock-free | 32-bit 平台不在目标范围 |
| platform_cpu/thread | ✅ 完全复用 | 零修改 |
| platform_memory | ✅ 32 字节对齐 | AVX-512 时再评估 64 字节 |

---

## 📊 决议执行优先级

| 优先级 | 决议 | 行动 |
|--------|------|------|
| P0 | D-091 | 启动 Path A POC 实现（C1 验证） |
| P0 | D-093 | 确认独立库架构（C3 设计决策） |
| P1 | D-092 | 实施 B1 POC benchmark（C2 验证） |
| P1 | D-095 | Path A MVP 基准测试（C5 性能基线） |
| P2 | D-094 | AVX-512 编译宏预留（C4 设计预留） |
| P3 | D-096 | XXH32→XXH64 迁移（Bloom 优化） |
