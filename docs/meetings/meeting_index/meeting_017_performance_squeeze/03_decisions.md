---
title: 性能压榨空间研讨会 — 决议
meeting_id: meeting_017_performance_squeeze
status: Reviewing
created_at: 2026-06-04
updated_at: 2026-06-04
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_017_performance_squeeze/meeting_README.md
parent_task: root
---

# 性能压榨空间研讨会 — 决议

## 投票统计

| 决议 | 内容 | Arch | Algo | Backend | Sec | Fullstack | 结果 |
|------|------|:---:|:---:|:---:|:---:|:---:|:---:|
| **D-130** | PGO + LTO + 64B 对齐 立项 | ❌ → ✅ | ✅ | ✅ | ✅ | ✅ | **4/5 通过 (Arch 被裁定覆写)**, 🔒 |
| **D-131** | Huge Pages POC (沿用 ACT-40) | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |
| **D-132** | 软件预取距离调优立项 | -- | ✅ | ✅ | ⚠️ | -- | **条件通过** 🔒 |
| **D-133a** | 热键缓存 workload 埋点 | ✅ | ✅ | -- | ⚠️ | -- | **3/3 通过**, 🔒 |
| **D-133b** | 热键缓存完整实现 (条件性) | ✅ | ✅ | -- | ⚠️ | -- | **埋点通过后启动**, 🔒 |
| **D-134** | 跳过原子读 → No-Go | -- | -- | ❌ | ✅ | -- | **Sec 行使安全否决权**, 🔒 |
| **D-135** | `find_with_hint` API 调研 | -- | -- | -- | -- | ✅ | **P2 调研**, 🔒 |
| **D-136** | 批量/排序批量 API → 暂缓 | -- | -- | -- | -- | ✅ | **等待业务需求**, 🔒 |
| **D-137** | 伪命题方向归档 | ✅ | ✅ | ✅ | -- | -- | **全员归档**, 🔒 |
| **D-138** | 项目进入"定向 P1 优化"模式 | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |
| **D-139** | G1-G5 门禁评估延后到 D-130~133b 完成后 | ✅ | ✅ | ✅ | ✅ | ✅ | **5/5 通过**, 🔒 |

---

## 每条决议的详细说明

### D-130: PGO + LTO + 64B 对齐 → 立项 P1

**核心论据**:
- Backend 引用同行数据: jemalloc PGO 5-8%, RocksDB LTO+PGO 8-12%, nginx PGO 3-5%, Linux kernel LTO+PGO 6-10%
- Algo 论证: PGO 让 `dir_lookup` 内联并紧贴 caller,消除 call/ret(5-10cy),优化分支预测方向
- 总预期: **5-15%** (470 → 400-445 cy)
- 投入: **2-3 天** (Makefile 加 target + 跑 workload + 链接产物)

**实现范围**:
1. Makefile 增加 `prof-gen` / `prof-use` target
2. 主 build 加 `-flto -fno-fat-lto-objects`
3. `aligned_alloc` 改为 64B 对齐(vals 起点)
4. LTO+PGO 集成 CI(Linux runner,8GB 内存下限)

**验收**:
- 均匀 50% B1 路径: 470 → ≤ 420 cy (5% 提升)
- 二进制大小变化: ±15%
- ASan/UBSan 复测零告警(Sec 防御)

**⚠️ 冲突 C-1 裁定记录**:
- Arch 原立场: "放弃,落维护"
- Host 裁定理由: Arch 估算"投入 > 1 周"偏高(实际 2-3 天),Backend 同行数据 + Algo 微架构分析支持 ROI
- 后续追踪: 若 PGO 收益 < 3%,降级归档

---

### D-131: Huge Pages POC (沿用 ACT-40)

**核心论据**:
- Algo 定量: L2 TLB miss 82cy → ~10cy,**确定性 -60~70cy**
- Arch 验证: 84MB 工作集压缩到 42 个 2MB 页,L2 TLB(~6MB)完全覆盖
- Linux `madvise(MADV_HUGEPAGE)` 启用,2 小时落地

**实现路径** (已在 ACT-40):
1. `perf stat -e dTLB-load-misses` 确认 TLB miss 占比
2. `platform_memory.c` 实现 2MB 对齐分配
3. Benchmark 对比 4KB vs 2MB 页面性能
4. 文档标注: 1M 场景收益衰减至 ~1.1x,需规模敏感度说明

**已知限制**:
- OS 依赖 Linux `transparent_hugepages`
- Windows 不支持
- 不阻塞 Int64 Phase 2

---

### D-132: 软件预取距离调优 → 立项 P1

**核心论据**:
- Algo 估算: 当前 470cy 中内存读 200cy 包含 L3 miss 惩罚(~40-60cy/次),如果预取距离使 L3 命中率从 ~70% 提到 ~90%,直接砍掉 30+ miss 惩罚
- 预期: **-30~50cy** (470 → 420-440 cy)

**实现范围**:
1. 跑 `perf stat -e cache-misses,cache-references` 建立 baseline
2. 微调 `__builtin_prefetch` 的 distance/locality 参数 (3-5 组实验)
3. 用 `callgrind` 验证 L3 命中率改善

**⚠️ Sec 缓解要求**:
- 强制 `if (i + stride < n) _mm_prefetch(..., _MM_HINT_NTA)` 防止越界
- 多线程场景需评估 cache 侧信道
- TSan 全量复测零告警

**预期收益**: 均匀 50% 场景 -7~10%

---

### D-133a: 热键缓存 workload 埋点 (P1 探针,2 天)

**核心论据**:
- Arch 方案: 先用 1-2 天埋点+真实 trace 回放验证 top-1% 命中率
- Algo 估算: Zipf α=1.0,256 项 cache 期望 2.83x (1560 → 600 cy)
- **关键阈值**: 若 top-1% 命中率 > 40% 投入完整实现,否则归档

**实现路径**:
1. 在 `benchmark/bench_data_gen.c` 增加 zipf 分布生成
2. 在 `search_b1.c` 入口埋点,记录 key 频次分布
3. 跑 1M queries 收集数据
4. 输出 `top-1%` / `top-5%` / `top-10%` 命中率
5. 决定 D-133b 是否启动

**⚠️ Sec 缓解要求**:
- 埋点代码需可静态关闭(`#ifdef INT32_SEARCH_PROFILE_KEYS`)
- 埋点数据不入版本控制(避免污染)
- TSan 全量复测

---

### D-133b: 热键缓存完整实现 (条件性 P1,1-2 天)

**触发条件**: D-133a 验证 top-1% 命中率 > 40%

**实现范围**:
1. 256-1024 项 direct-mapped cache
2. 1-4KB 内存开销
3. 缓存失效绑定 `rebuild_gen` 字段,版本失配立即降级主表
4. ~200 行实现

**验收**:
- Zipf α=1.0 场景: 1560 → ≤ 700 cy (2.2x 提升)
- 均匀 50% 场景: 不退化
- 1M rebuild × 并发读测试零悬挂引用

**失败处置**: 验证不通过 → 归档,标注 `[DEBT-HotKeyCache] 实际 workload 不集中,YAGNI`

---

### D-134: 跳过原子读 → No-Go

**裁定理由**:
- Backend 提议: 热路径消除 atomic 读,可省 10x 延迟
- Sec 安全否决: 破坏 acquire/release 内存序,Int64 rebuild UAF 历史会重演
- **ROI 计算**: 一次 atomic 读 vs 普通读差 ~1-2ns,每次查询 1-2 次 atomic 读,总节省 < 5cy (< 1%)
- 风险等级: HIGH (UAF 历史)

**结论**: `[DEBT-SkipAtomic] 收益 < 5cy, 风险 UAF, ROI 极差, 关闭。`

---

### D-135: `find_with_hint` API 调研 (P2)

**核心论据**:
- Fullstack 提议: 零迁移成本,`hint = last_result` 模式在流量表、时序数据上能拿 **20-50%** 收益
- 投入产出比最高

**实现范围**:
1. 评估接口签名:`int int32_search_find_with_hint(int32_search_t, int32_t key, size_t hint, size_t* out_index)`
2. hint 优先级: hint 附近 ±4 → 主 B1/A 路径
3. 调用方文档: 时序数据 / 局部性场景推荐使用
4. 渐进式公开(实验性头文件)

**触发条件**: 等待具体业务需求场景 (D-136 同)

---

### D-136: 批量/排序批量 API → 暂缓

**Fullstack 评估**:
- N=16 批量查询理论 1.5-2.5x 加速,瓶颈在分支预测和 L1 命中率
- 大数据 > L1 时,SIMD 并行收益被 cache miss 吞掉,真实场景 1.2-1.5x
- ABI 兼容性 OK (纯追加)
- 误用率高 (业务 80% 是单 key)

**结论**: 等待具体业务场景(决策树批量推理、RocksDB MultiGet 类)再启动 P2 调研。**当前不立项**。

---

### D-137: 伪命题方向归档

| 方向 | 归档标注 |
|------|----------|
| van Emde Boas 布局 | `[DEBT-vEB] 均匀随机访问反模式,meeting_017 算法分析否决` |
| 3-byte 编码 | `[DEBT-Compression] 瓶颈是延迟不是带宽,解压进关键路径负收益` |
| 跳表 / B+ / Radix | `[DEBT-NewStructures] 静态 10M 排序集点查无优势` |
| 手写汇编 | `[DEBT-Asm] GCC 已能输出几乎相同代码,维护成本压垮收益` |
| `__attribute__((flatten))` | `[DEBT-Flatten] 二进制膨胀 2-3×,冷函数被错误内联` |
| ARM NEON (沿用 D-120) | `[DEBT-NEON] 远期,无目标平台` |
| AVX-512 (沿用 D-119) | `[DEBT-AVX512] 关闭,降频 > 收益` |
| Eytzinger (沿用 D-121) | `[DEBT-Eytzinger] meeting_007 POC 证伪` |
| RMI (沿用 D-123) | `[DEBT-RMI] B1 已特化` |

---

### D-138: 项目进入"定向 P1 优化"模式

**核心判断**:
- 不直接进入"维护模式"(G1-G5 门禁评估)
- 也不立项 Phase 4(均匀场景总潜在空间 1.3-1.4x,达不到立项阈值)
- 启动 **4 个 POC**, 6-8 天投入
- POC 全部跑完后**再评估**是否进入维护模式

**新增门禁 (G6)**:
- G6: D-130~133b POC 全部完成 + 文档化

---

### D-139: G1-G5 门禁评估延后

**判断**:
- G1 (Int32 Feature Complete) ✅
- G2 (Int64 Phase 2 完成) ⏳
- G3 (CI/CD) ⏳
- G4 (TODO P1 闭合) ⏳
- G5 (MinGW 退化实验) ✅ (ACT-33/34 已完成)

**新门禁 G6**: 性能 POC 4 项完成
- G6 = G1 + G2 + G3 + G4 + G5 + (D-130~133b 完成)

---

## 决议路线图

```
✅ 本周 (D-130 ~ D-132 启动)
  ├── D-130 PGO + LTO (Backend 主导, 2-3 天)
  ├── D-131 Huge Pages POC (Algo 主导, 1-2 天, 沿用 ACT-40)
  └── D-132 预取调优 (Algo 主导, 1-2 天)

🔧 下周 (D-133a 启动, P1 探针)
  └── D-133a 热键缓存埋点 (Algo 主导, 2 天)

⏳ 条件性 (D-133b)
  └── D-133b 热键缓存完整实现 (1-2 天, 仅当 D-133a 验证通过)

🔄 重叠进行
  ├── Int64 Phase 2 立项 (D-116 ACT-38)
  ├── CI/CD 基础版 (D-124 ACT-35)
  └── TODO-06/07 (D-115 ACT-36/37)

📅 4-5 周后
  └── 性能 POC 4 项全部完成 → 重新评估 G6 门禁 → 决定进入维护模式
```

---

## 总预期性能目标

| 场景 | 当前 | POC 后目标 | 提升 |
|------|------|-----------|------|
| 10M uniform 50% (B1) | 470 cy | **330-360 cy** | **1.30-1.42x** |
| 10M Zipf α=1.0 (B1) | 1560 cy | **600-700 cy** | **2.2-2.6x** (条件) |
| 1M uniform 50% (B1) | 282 cy | ~250 cy | 1.13x (收益衰减) |
| 1M Zipf α=1.0 (B1) | -- | -- | 取决于 D-133a/b |
