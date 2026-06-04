---
title: 讨论记录 — 布隆过滤器开关特性
meeting_id: meeting_013_bloom_toggle
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
---

# 讨论记录 — 布隆过滤器开关特性讨论会议

## 第一轮讨论（5 Agent 并行）

### 议题1：是否提供查询级 Bloom 旁路能力

| Agent | 立场 | 核心论据 |
|-------|------|----------|
| **Architect** | **Go，推荐B** | 架构正交性（Bloom是独立预筛层）；Option B 防止API函数名爆炸；`_ex`后缀兼容模式成熟；反对C（cfg将构建时配置与查询时语义混淆） |
| **Algorithm** | **Go，推荐C** | Bloom开销被低估：实际~50cy而非20-35cy（取模division+位数组L3访问）；Path B1下bloom占70%开销；反对A（API膨胀）；反对B（破坏ABI） |
| **Backend** | **Go，强烈推荐C** | C是最小侵入方案（~5行代码变更）；不改API签名；_Atomic relaxed无并发问题；反对B（破坏ABI，所有调用点需修改）；反对A（API膨胀） |
| **Fullstack** | **Go，推荐C，备选A** | C认知模型自然（Bloom是句柄属性）；使用场景匹配（批量切换）；反对B（破坏v1.0.0-rc用户信心）；不反对D |
| **Security** | **推荐D，次选B** | C被评为最危险（TOCTOU UAF风险）；推荐编译时切换已足够；若必须运行时则选B+白名单掩码；反对A（代码分叉安全债务） |

### 议题1投票汇总

| 方案 | 推荐 | 可接受 | 反对 |
|------|------|--------|------|
| A (find_no_bloom) | — | Fullstack(备选), Architect(备选) | Algorithm, Backend |
| B (find_ex + flags) | Architect | Security(次选) | Backend, Algorithm, Fullstack |
| **C (内部bloom_bypass)** | Backend, Algorithm, Fullstack | — | **Security(危险)**, Architect(关注点分离) |
| D (不提供) | Security | Fullstack | Algorithm, Backend, Architect |

### ⚠️ 冲突点确认

**冲突1**：B vs C 的 API 设计选择
- Architect 力推 B（flags 位掩码，可扩展），认为 C 违反关注点分离
- Backend/Algorithm/Fullstack 反推 C（最小侵入，不破ABI），认为 B 破坏 ABI 是致命伤

**冲突2**：C 的安全评估
- Security 评为最危险方案（TOCTOU UAF），推荐 D（不提供）
- Backend 论证 _Atomic relaxed load 安全无虞

### 议题2：对"确定存在"场景的性能影响

| Agent | 核心观点 |
|-------|----------|
| **Algorithm** | 实际开销~50cy：XXH32×3=24cy + 取模×3=15cy + 位检查L3=12cy + atomic_load=1cy。Path B1占比70%，Path A占比31%。开销与数据规模无关，是串行叠加的。 |
| **Architect** | 架构层面分析：Bloom对"确定不存在"key是绝对价值（~100cy避免~200cy），对"确定存在"key是纯浪费。提供旁路是补充缺失的语义表达。 |
| **Backend** | 创建了动态场景需求：高峰写流量vs低谷回放流量，同一进程切换。create-time配置无法覆盖。 |
| **Fullstack** | 认可性能收益，强调测试需L3层（性能差异化验证） |
| **Security** | 无独立性能意见 |

### 议题3：运行时 Bloom 开关的设计空间

| 维度 | Architect(B) | Algorithm(C) | Backend(C) | Fullstack(C) | Security(D/B) |
|------|-------------|-------------|-----------|-------------|---------------|
| API签名 | find_ex + flags | cfg.bloom_bypass + impl字段 | bloom_skip setter | bloom_enable setter | 编译时切换 |
| ABI兼容 | 向后兼容(find保留,find_ex新增) | 完全兼容 | 完全兼容 | 完全兼容 | 完全兼容 |
| 热路径 | flags寄存器判断 | cacheline内uint8判断 | _Atomic relaxed判断 | _Atomic relaxed判断 | 无额外开销 |
| COW交互 | 零影响(栈参数) | 零影响 | 零影响 | 零影响 | 零影响 |

### 议题4：对 int64 扩展的影响

| Agent | 观点 |
|-------|------|
| **Architect** | 强烈建议同步支持，int64 v0.1.0 即内置 find_ex+SKIP_BLOOM |
| **Algorithm** | 布隆零算法改动迁移；int64下bloom占比仍>60% (Path B1)；建议独立验证假阳性率 |
| **Backend** | 采用与int32相同机制，命名一致。代码模式复制，不重新发明 |
| **Fullstack** | infrafirst策略：先int32定稿→泛化基础设施→再同步int64 |
| **Security** | 发现3个并发问题（bloom交换顺序→假阴性窗口、path非原子读写、B1三指针不原子）需优先修复 |

---

## 🔄 第二轮：冲突交叉讨论

### 冲突1：B vs C — API 设计哲学

收到 Architect 与 Backend/Algorithm 的冲突后，进行交叉讨论：

#### Backend 回应 Architect 的 B 方案质疑

Architect 的 B 方案（`find_ex` + flags）确实在可扩展性上更优。但我认为当前场景下可扩展性被高估了：

1. **实际需要多少个 flags？** 布隆旁路可能是唯一需要在查询时传递的标志。Path A/B1 自动选择发生在创建时，SIMD vs Scalar 是内部实现细节不暴露。未来可能的新增标志（如"纯标量模式"用于调试）更像是 Niche 需求，不足以 justify 新增 `_ex` 后缀的复杂度。

2. **_ex 后缀的命名噪音**：`find_ex` vs `find` 两套并存的 API 对用户的认知负担不低于新增一个简单的 `find_no_bloom`。实际上，如果用户需要的是"这个句柄暂时不用 bloom"，一个 setter 函数最直觉。

3. **ABI 兼容性仍然是真实顾虑**：即使 v1.0.0-rc，已经有用户在测试集成（12套测试全部通过）。改变 `find` 签名（即使是通过新增 `_ex` 变体来避免破坏）会让头文件出现两个几乎相同的函数声明，用户在 code review 时会困惑。

#### Architect 回应 Backend 的 C 方案质疑

理解 Backend 对 ABI 的担忧。但我对 C 方案的架构完整性有所顾虑：

1. **cfg 的语义边界**：`int32_search_config_t` 的语义是"构建时的数据特征"，而 `bloom_bypass` 是"查询时的行为策略"。把后者放进前者，类型定义层面出现了语义混淆。如果后续再有查询时选项（如调试模式），cfg 结构体就会变成一个杂乱的"构建+查询混合配置"袋子。

2. **rebuild 交互**：如果用户在 cfg 中设置 `bloom_bypass=1` 然后调用 `rebuild()`，此时 rebuild 应该尊重这个设置（跳过 bloom 构建）还是仍然构建 bloom（但查询时绕过）？这引入了语义歧义。

3. **但我不再坚持强制选 B**。我对 "C 方案用内部字段而非 cfg 字段" 的折衷方案持开放态度——即在 `int32_search_impl_t` 中新增 `_Atomic(int) bloom_bypass`，并通过独立的 `int32_search_set_bloom_bypass(handle, int bypass)` setter 函数来操作，而不是把它塞进 `int32_search_config_t`。这样就同时解决了：ABI 兼容（签名不变）、关注点分离（cfg 仍然是纯构建配置）、可扩展性（setter 模式可以扩展到其他运行时行为）。

### 冲突2：C方案的安全评估

#### Security 的 UAF 风险评估与 Backend 的回应

Security 担心的 TOCTOU UAF 场景：
> "写线程将 cfg->use_bloom 从 1 改为 0 → 读线程刚加载 impl->bloom 指针（非空）→ 写线程释放 bloom 内存 → 读线程访问 bloom_query → UAF"

**Backend 回应**：这个场景的前提是"修改 cfg 字段会触发 bloom 内存释放"，但方案 C 的语义并非如此。`bloom_bypass` 是一个纯 hint，只影响 `find()` 中的条件判断：

```c
// 方案 C 的正确实现（非 cfg 字段，而是内部字段 + setter）：
// setter: int32_search_set_bloom_bypass(handle, bypass)
// 仅设置 impl->bloom_bypass = bypass，不涉及任何内存释放！
// bloom 指针本身完全不受影响，仍在 COW rebuild 时正常管理。

// find() 热路径：
if (!impl->bloom_bypass && bf != NULL && !bloom_query(bf, key)) return NOT_FOUND;
```

**不存在 UAF 路径**。`bloom_bypass` 的修改不触发任何 `free()`、不操作 `bloom` 指针、不涉及 reader_count 同步。它只是一个 `_Atomic(int)` 的 relaxed 写入。

**但是**，我承认我的初始描述（"cfg 字段"）确实会引发这个误解。**修正为：不用 `int32_search_config_t` 的字段，而用 `int32_search_impl_t` 的内部字段 + 独立的 setter 函数。** 这完全消除了 Security 担心的 TOCTOU 风险。

#### Security 的回应

接受 Backend 的澄清。如果 `bloom_bypass` 是 `int32_search_impl_t` 的内部字段 + 独立 setter，不触发 bloom 内存释放，则：
- **UAF 风险解除**。无需再担心 TOCTOU。
- 但仍需注意：如果 `bloom_bypass` 使用 `_Atomic` 并在 `find()` 中用 `memory_order_relaxed` 加载，这是安全的（与 bloom 指针的可见性无因果关系）。
- 唯一的小建议：setter 函数应文档化"不保证即时生效（在已发起的 find() 调用中），但保证后续 find() 调用可见"——这符合 relaxed 内存序的语义。

### 冲突解决状态

| 冲突 | 涉及方 | 解决方案 | 状态 |
|------|--------|----------|------|
| B vs C | Architect vs Backend/Algorithm/Fullstack | Architect 接受 C 的折衷方案（内部字段+setter，非cfg字段） | ✅ 已解决 |
| C 安全性 | Security vs Backend | Security 接受 Backend 的澄清（非cfg字段，不改bloom指针） | ✅ 已解决 |

### 融合方案

**方案 C'（精炼版）**：

1. **不修改 `int32_search_config_t`**（保持 cfg 为纯构建配置）
2. **在 `int32_search_impl_t` 中新增 `_Atomic(int) bloom_bypass`**（初始值 0）
3. **新增 `int32_search_set_bloom_bypass(handle, bypass)` 函数**（运行时 setter，仅修改原子字段，不触发内存管理）
4. **`find()` 热路径增加 `!impl->bloom_bypass` 条件**（使用 `memory_order_relaxed` 加载，~1 cycle）
5. **rebuild() 保持 bloom_bypass 状态不变**（作为句柄属性持久化）
6. **`find_range()` 保留现状**（当前不使用 bloom，不受影响）

---

## 📊 最终共识结论

### 议题1：Go — 提供 Bloom 旁路能力（5/5 达成一致）

经过冲突交叉讨论，全体 Agent 就方案 C'（内部字段+setter）达成一致。

### 议题2：性能影响确认

- Bloom 预筛实际开销约 50 cycles（XXH32×3 + 取模 + 位检查）
- Path B1 下最多占 70% 查询延迟
- "确定存在"场景为纯浪费
- 旁路可消除此开销

### 议题3：具体设计（方案 C'）

| 设计项 | 决策 |
|--------|------|
| API 形态 | 新函数 `int32_search_set_bloom_bypass()` + 内部 `_Atomic(int) bloom_bypass` |
| CFG 兼容 | `int32_search_config_t` 不变，`use_bloom` 语义不变 |
| 热路径 | `if (!impl->bloom_bypass && bf && !bloom_query(bf, key))` |
| 线程安全 | `memory_order_relaxed` 原子操作，与 COW 模型零冲突 |
| rebuild 行为 | 保持 `bloom_bypass` 状态不变 |
| ABI 兼容 | 完全兼容（纯增量） |

### 议题4：int64 同步支持

| 决策 | 内容 |
|------|------|
| 同步时机 | int64 库的第一个版本即内置此特性 |
| API 命名 | `int64_search_set_bloom_bypass()` — 与 int32 保持一致 |
| 实现策略 | infrafirst：int32 先实现并验证 → int64 模式复制 |

### 🔒 发现的并发问题（来自 Security 审计）

以下问题不属于本次议题范畴但值得记录，建议创建独立行动项：

1. **【P1】rebuild() bloom 交换顺序 → 假阴性窗口**：数据交换在前、bloom 交换在后，导致瞬时窗口内新数据+旧bloom的组合可能产生假阴性
2. **【P2】`impl->path` 非原子读写**：C11 标准下的数据竞争，x86 上实际崩溃概率低
3. **【P3/LOW】B1 三指针非原子批量交换**：已知设计权衡，已有文档记录
