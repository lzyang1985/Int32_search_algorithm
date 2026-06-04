---
title: 优化方向讨论会 — 决议
meeting_id: meeting_016_optimization_direction
status: Draft
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_016_optimization_direction/meeting_README.md
parent_task: root
---

# 优化方向讨论会 — 决议

## 投票统计

| 决议编号 | 内容 | Arch | Algo | Backend | Sec | Host | 结果 |
|----------|------|:---:|:---:|:---:|:---:|:---:|:---:|
| D-115 | TODO-01~05 → P0 立即执行 | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过** 🔒 |
| D-116 | Int64 Phase 2 范围: COW+Bloom重建+broadcast hoisting | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过** 🔒 |
| D-117 | Int64 COW 方案: 复用 Int32 逐字段 atomic+reader_count（不用spinlock） | ✅ | — | ✅ | ✅ | ✅ | **4/4 通过** 🔒 |
| D-118 | Int64 当前 rebuild 标注"单线程 only"安全警告 | ✅ | — | ✅ | ✅ | ✅ | **4/4 通过** 🔒 |
| D-119 | AVX-512 → No-Go, 代码框架保留, TODO-15 关闭 | ✅ | ✅ | — | ✅ | ✅ | **4/4 通过** 🔒 |
| D-120 | ARM NEON → P3, 有 ARM 需求时 POC | ✅ | ✅ | — | ✅ | ✅ | **4/4 通过** 🔒 |
| D-121 | Eytzinger → 归档（meeting_007 POC 0.45x 证伪） | ❌ → ✅ | ✅ | — | — | ✅ | **Arch 被裁定覆写, 3/3 通过** 🔒 |
| D-122 | 倾斜分布优化: 推荐热键缓存 POC (P1) | ✅ | ✅ | — | — | ✅ | **3/3 通过** 🔒 |
| D-123 | RMI → No-Go, B1 已是特化学习型索引 | ✅ | ✅ | — | — | ✅ | **3/3 通过** 🔒 |
| D-124 | CI/CD 基础版 → P1 必须建立（Linux GCC + ASan/UBSan/TSan） | ✅ | — | ✅ | ✅ | ✅ | **4/4 通过** 🔒 |
| D-125 | MinGW 退化: 先执行 D-037 实验 → 基于结果决策 | ✅ | ✅ | ✅ | — | ✅ | **4/4 通过** 🔒 |
| D-126 | 项目进入维护模式门禁: G1-G5 全部满足后 | ✅ | — | ✅ | ✅ | ✅ | **4/4 通过** 🔒 |
| D-127 | Int64 int32_t dir → int64_t dir → No-Go (YAGNI) | ✅ | — | — | — | ✅ | **2/2 通过** 🔒 |
| D-128 | Int64 find_range B1 → P2, 等待需求驱动 | ✅ | — | — | — | ✅ | **2/2 通过** 🔒 |
| D-129 | Huge Pages → 独立 POC (P1), 不阻塞 Phase 2 | ✅ | ✅ | — | — | ✅ | **3/3 通过** 🔒 |

---

## 每条决议的详细说明

### D-115: TODO-01~05 → P0 立即执行

**内容:** 本次会议后一次性清掉 5 项低风险改进。

| TODO | 内容 | 类型 | 工作量 |
|------|------|------|--------|
| TODO-01 | unused variable 警告 | Fix | 5 分钟 |
| TODO-02 | 溢出检查 | Optimization | 5 分钟 |
| TODO-03 | SIMD broadcast 提升 | Optimization | 5 分钟 |
| TODO-04 | Makefile int64 target | Config | 1-2 小时 |
| TODO-05 | .gitignore | Config | 10 分钟 |

**总工作量: ~2-3 小时**

---

### D-116: Int64 Phase 2 范围

**交付物:**
1. COW 多线程（核心交付）
2. Bloom 重建逻辑（低挂果实）
3. TODO-03 broadcast hoisting（P0 附带）

**不含:**
- dir int64_t 迁移（YAGNI, D-127）
- find_range B1（等待需求, D-128）

**验收标准:** TSan 零告警、ASan/UBSan 零告警、rebuild 后 bloom 状态保持

---

### D-117: Int64 COW 方案修正

**原 TODO-10 假设需要 spinlock（24 字节 > 16 字节 lock-free 上限）→ 不成立。**

采用 Int32 B1 COW 已验证方案:
- `vals`、`dir`、`n` 各自 `_Atomic` → 每个 8 字节 lock-free
- `reader_count` 保护旧内存释放
- 逐个原子交换（release/acquire 语义）

**理由:** Int64 B1 只有 2 指针（vals + dir），比 Int32 B1 的 3 指针更简单。

---

### D-118: Int64 rebuild 安全警告

**发现问题:** [api_int64.c:L150-L154](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/api_int64.c#L150-L154) 中 rebuild 直接 free 旧数据，无并发保护。

**立即动作:** 在 README.txt 和 `include/int64_search.h` 文档注释中标注：
```
⚠️ 当前 rebuild() 仅支持单线程场景。在并发 rebuild 场景下存在 use-after-free 风险。
多线程支持将在 Int64 Phase 2 (COW) 中实现。
```

---

### D-119: AVX-512 → No-Go

**核心论据 (Algo 定量分析):**

B1 路径性能结构中，SIMD 比较仅占 7%（~21 cycles）。AVX-512 能节省的 ~9 cycles 被 15-20% 的降频惩罚（+60 cycles）完全抵消，**净效果为负收益**。

**动作:** TODO-15 标记为 `[DEBT-AVX512] 关闭。` 代码框架（`#ifdef INT32_SEARCH_AVX512`）保留。

---

### D-121: Eytzinger → 归档

**⚠️ 原 Arch 意见为 Go P1，经 Algo 提供 meeting_007 POC 决定性数据（0.45x @ 10M, Linux GCC）后，Host 裁定采纳 Algo 意见。**

POC 数据来源: meeting_007 POC-04~07，Linux Xeon Gold 6152 + GCC 11.4.0，rdtscp+lfence 精确定时。

标记: `[DEBT-Eytzinger] meeting_007 POC 证伪 (0.45x)，分支预测和缓存模式根因已确认，归档。`

---

### D-122: 热键缓存 POC

**由 Algorithm Agent 提出。** Zipf α=1.0 数据的热键缓存预估：~2.83x vs 标量二分（当前退化为 1560 cy/q → ~551 cy/q）。

**POC 参数:**
- 64-256 条目 direct-mapped 缓存
- 1-4KB 内存开销
- 实现复杂度: ~200 行
- 需要处理 COW rebuild 时的缓存失效

**建议:** 独立快速 POC，不阻塞 Int64 Phase 2。

---

### D-124: CI/CD 基础版

**最小可行 CI 流水线（GitHub Actions, Linux runner）:**

```yaml
- make test                     # 基础测试
- make test CFLAGS="... -fsanitize=address,undefined"  # ASan/UBSan
- make test-thread              # TSan
- make test-b1                  # B1 测试
- make test-range               # range 测试
```

**前置依赖:** TODO-04 (Makefile int64 target)

---

### D-125: MinGW 退化 — D-037 实验

**meeting_005 已决议但从未执行。** 实验内容：
- E1: `-mavx2` (当前基线) → 0.45x-0.55x
- E2: `-march=native` → 预期 3.53x
- E3: 不同 GCC 版本对比
- E4: 必要时 Clang for Windows 对比

**预估时间: ~30 分钟**

**决策逻辑:**
- `-march=native` 恢复性能 → Makefile 默认使用 `-march=native`（一行改动）
- 仍为 0.45x → 接受"Windows=B1/标量"，记录为已知限制
- 同时调研 Clang for Windows（~1 天），基于数据决定是否集成

---

### D-126: 维护模式门禁

| # | 门禁 | 状态 |
|---|------|------|
| G1 | Int32 库 Feature Complete | ✅ 已满足 |
| G2 | Int64 Phase 2 (COW+Bloom重建) 完成 | ⏳ 待立项 |
| G3 | CI/CD 基础版建立 | ⏳ 待 TODO-04 完成后 |
| G4 | TODO P1 项全部闭合 | ⏳ TODO-01~05 P0, TODO-06/07 P1 |
| G5 | D-037 MinGW 退化实验有定论 | ⏳ 30 分钟实验 |

**目标: G1-G5 全部满足后，项目进入"Feature Complete + 维护模式"。**

---

### D-129: Huge Pages POC

**由 Algorithm Agent 提出。** B1 路径在 10M 数据下，工作集 80MB + 4MB = 84MB 远超 L2 TLB 覆盖范围（~6MB），TLB miss 是主要瓶颈之一。

**预估收益:** 消除 TLB miss → ~220 cy/q（vs 当前 318, 1.45x）

**POC 内容:**
1. 用 `perf stat -e dTLB-load-misses` 确认 TLB miss 占比
2. 在 platform_memory.c 中实现 2MB 对齐分配（`posix_memalign` + `madvise(MADV_HUGEPAGE)`）
3. Benchmark 对比 4KB vs 2MB 页面性能

**注意:** 不阻塞 Int64 Phase 2。OS 依赖（Linux `transparent_hugepages`），Windows 不支持。
