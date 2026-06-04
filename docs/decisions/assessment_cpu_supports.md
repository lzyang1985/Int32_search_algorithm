---
title: 风险评估 — __builtin_cpu_supports 假阳性 #UD 崩溃
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Architect
task_id: task_001_phase1_mvp
parent_doc: "docs/tasks/task_001_phase1_mvp/FIXPLAN_task_001_phase1_mvp.md"
parent_task: task_001_phase1_mvp
source_docs:
  - "docs/tasks/task_001_phase1_mvp/TODO_task_001_phase1_mvp.md"
  - "docs/tasks/task_001_phase1_mvp/FIXPLAN_task_001_phase1_mvp.md"
  - "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
trace_code:
  - "src/platform_cpu.c"
  - "src/api.c"
tags: [security, avx2, cpu-detection, false-positive, risk-assessment]
---

# 风险评估 — `__builtin_cpu_supports` 假阳性 #UD 崩溃

## 1. 背景

项目通过 [platform_cpu.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/src/platform_cpu.c#L15-L22) 中的 `platform_cpu_has_avx2()` 检测当前 CPU 是否支持 AVX2：

```c
int platform_cpu_has_avx2(void)
{
    static int cached = -1;
    if (cached == -1) {
        cached = __builtin_cpu_supports("avx2") ? 1 : 0;
    }
    return cached;
}
```

`api.c` 中 `int32_search_create()` 根据此函数返回值决定是否将 `search_fn` 指向 AVX2 路径。若 `__builtin_cpu_supports("avx2")` 返回 true，但 CPU 实际上**无法执行** AVX2 指令，则首次查询时触发 `#UD`（Illegal Instruction / Undefined Opcode）→ 进程 SIGILL 崩溃。

此风险在 meeting_004 审计阶段由 Code Security Expert 首次提出（S-TODO-03），经 ACT-15 转入 FIXPLAN 第五波 DEEP-04，并在 meeting_005 安全跟踪表中列为 SEC-04。

---

## 2. 触发场景分析

| 场景 | 概率 | 后果 |
|------|------|------|
| **虚拟机/KVM 配置错误**：宿主机支持 AVX2，但 VM 未暴露 AVX2 | 低 | CPUID 不报告 AVX2，`__builtin_cpu_supports` 返回 false — **不触发** |
| **老版 QEMU/TCG 模拟器**：CPUID 模拟不完整，报告 AVX2=yes 但不实现指令 | 低-中 | **可能触发** #UD |
| **容器/Hyper-V 隔离**：CPU 特性传递不完整 | 极低 | 现代 hypervisor 通常正确传递 CPUID |
| **Wine/兼容层**：Windows 二进制在 Linux 上运行 | 低 | 取决于 Wine 版本，旧版可能不完整 |
| **GCC `__builtin_cpu_supports` 自身缺陷**：特定 GCC 版本/Vendor 可能有 bug | 极低 | GCC 11.4.0（项目目标版本）无已知相关 bug |
| **正常硬件**：物理 Intel/AMD CPU 报告 AVX2 即支持 | — | **不触发** |

> **结论**：高风险场景集中在**非标准虚拟化/模拟环境**。正常物理机和主流 hypervisor（KVM/VMware/Hyper-V）不受影响。

---

## 3. 当前代码的脆弱点

```
platform_cpu_has_avx2() → true
    ↓
int32_search_create() 中 impl->search_fn = search_avx2_find
    ↓
int32_search_find() 首次调用
    ↓
search_avx2_find() 首条 AVX2 指令（vpbroadcastd ymm1, xmm1）
    ↓
💥 #UD → SIGILL → 进程崩溃
```

崩溃发生在**运行时、首次查询时**，而非创建时。这意味着：
- `int32_search_create()` 成功返回非 NULL（看似正常）
- `int32_search_find()` 第一次调用即崩溃
- 无恢复机会（`#UD` 是同步异常，C 语言无法捕获信号级别）

---

## 4. 防御策略评估

### 4.1 Safe Probe（推荐 ✅）

在 `platform_cpu_has_avx2()` 内部增加一条探测指令，用信号处理器保护：

```c
#include <signal.h>
#include <setjmp.h>

static sigjmp_buf probe_jmp;
static volatile int probe_sigill = 0;

static void probe_sigill_handler(int sig) {
    probe_sigill = 1;
    siglongjmp(probe_jmp, 1);
}

static int avx2_safe_probe(void) {
    signal(SIGILL, probe_sigill_handler);
    if (sigsetjmp(probe_jmp, 1) == 0) {
        __asm__ __volatile__("vzeroupper" ::: "ymm0");
    }
    signal(SIGILL, SIG_DFL);
    return probe_sigill == 0;
}

int platform_cpu_has_avx2(void) {
    static int cached = -1;
    if (cached == -1) {
        int cpuid_ok = __builtin_cpu_supports("avx2") ? 1 : 0;
        if (cpuid_ok) {
            cpuid_ok = avx2_safe_probe();
        }
        cached = cpuid_ok;
    }
    return cached;
}
```

| 维度 | 评价 |
|------|------|
| 安全性 | ✅ 彻底解决假阳性 — CPUID 通过但指令失败 → 回退标量 |
| 性能 | ✅ 仅在 `cached == -1` 时执行一次（首次调用），代价：一条 `vzeroupper` + `sigsetjmp` |
| 可移植性 | ✅ POSIX `signal` + `sigsetjmp`，Linux/macOS/BSD 通用；Windows 可用 SEH `__try/__except` |
| 复杂度 | ⚠️ 中等 — 引入信号处理 + 非本地跳转 |
| 副作用 | ⚠️ 若代码运行在多线程环境，`signal(SIGILL, ...)` 会影响全局。需改用 `sigaction` + 线程局部策略 |

### 4.2 编译时旁路（部分缓解）

在编译时已知目标 CPU 时，用 `-march=native` 可让 GCC 内联 AVX2 指令而不依赖运行时检测，但：
- 无法解决运行时可执行文件在不同 CPU 上部署的场景
- 不适用于库（`.a`/`.so`）的通用分发
- **不推荐**作为唯一防御

### 4.3 不做任何处理（当前状态）

| 维度 | 评价 |
|------|------|
| 复杂度 | ✅ 无额外代码 |
| 安全性 | ❌ 在非标准虚拟化/模拟环境下有崩溃风险 |
| 可接受性 | ⚠️ 对 99%+ 的物理机/标准虚拟机无影响；但崩溃是静默的、不可恢复的 |

---

## 5. 推荐方案

| 方案 | 优先级 | 时机 | 说明 |
|------|--------|------|------|
| **Safe Probe**（POSIX 版） | P1 | Phase 2 启动前 | 在 `platform_cpu.c` 中实现 `sigsetjmp` + `vzeroupper` 探测 |
| **Safe Probe**（Windows SEH 版） | P2 | 跨平台适配时 | 用 `__try/__except` 替代 POSIX 信号 |
| **AVX-512 扩展** | P3 | 后续 | 若未来支持 AVX-512，Safe Probe 可扩展为探测多条指令 |

### 5.1 不推荐立即实施的原因

1. 当前所有目标环境（Ubuntu 22.04 KVM、物理 Intel Xeon、物理 AMD）均无假阳性报告
2. 代码仅在 `int32_search_create()` 时进行 CPU 检测，若检测通过但在查询时崩溃，属于**极度边缘**场景
3. Safe Probe 引入了信号处理依赖，需要额外的跨平台测试和线程安全审查

### 5.2 触发实施的条件

- 首次在非标准虚拟化环境中部署时
- 收到假阳性崩溃报告
- Phase 2 跨平台（ARM/Apple Silicon Rosetta 2 等）适配时

---

## 6. 风险评级

| 维度 | 评分 | 理由 |
|------|------|------|
| 严重程度 | Medium | 崩溃不可恢复，但仅影响非标准环境 |
| 发生概率 | Low | 正常物理机/标准 VM 不受影响 |
| 可检测性 | Medium | 崩溃即发现，但无前置告警 |
| 综合风险 | **Low-Medium** | 可接受，建议标记为 Known Limitation 并按需实施 |

---

## 7. 关联追踪

| 编号链 | 状态 |
|--------|------|
| S-TODO-03（meeting_004）→ ACT-15 → DEEP-04 → SEC-04 | ✅ 本文档完成 |
| 待办：Safe Probe 实现 | 待 Phase 2 启动前决策 |
| 待办：FINAL_task_001 "已知平台限制" 更新 | ✅ 已在 D-035 中完成 |

---

> 本风险评估完成。

---

## 归档元数据

| 字段 | 值 |
|------|-----|
| 原始路径 | docs/tasks/task_001_phase1_mvp/ASSESSMENT_cpu_supports_false_positive_task_001_phase1_mvp.md |
| 归档日期 | 2026-05-30 |
| 归档版本 | meeting_005 Reviewed (S-TODO-03/DEEP-04 完成) |
| task_id | task_001_phase1_mvp |
