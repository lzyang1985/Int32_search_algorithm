---
title: 待办事项 — Windows AVX2 异常调查结果审查会
status: Frozen
created_at: 2026-05-29
updated_at: 2026-05-30
author: Agent_Executor
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/meeting_README.md"
---

# 待办事项 — Windows AVX2 异常调查结果审查会

## P0 — 关键路径（立即执行）

| 编号 | 任务 | 类型 | 负责 | 关联决议 | 预估 |
|------|------|------|------|----------|------|
| TODO-01 | 本平台编译选项对比实验（E1-E4） | 测试 | Algorithm | D-037 | 30 min | ✅ 已完成 |
| TODO-02 | 实现构建时函数指针方案（internal.h + api.c） | 开发 | Backend | D-038 | 30 min | ✅ 已完成 |
| TODO-03 | 修订技术路线文档 D-008 表述 + §7 新增风险条目 | 文档 | Architect | D-036 | 15 min | ✅ 已完成 |
| TODO-04 | FINAL_task_001 新增"已知平台限制"条目 | 文档 | Architect | D-035 | 10 min | ✅ 已完成 |

### TODO-01 详细说明：编译选项对比实验

**目的**：区分"算法固有瓶颈"和"编译器代码生成质量"。

**实验矩阵**：

| 实验 | 编译选项 | 预期 |
|------|---------|------|
| E1 | `-O3 -mavx2`（当前基线） | 复现 ~951 cy/q (Raw AVX2, N=1M) |
| E2 | `-O3 -mavx2 -march=native` | 验证是否恢复到 ~150 cy/q |
| E3 | E2 汇编 vs E1 汇编（`objdump -d`） | 定位指令级差异 |
| E4 | POC `search_avx2_binary` 拷入当前项目编译 | 排除代码逻辑差异 |

**成功标准**：
- E2 达到 ≥3.0x vs 标量 → 根因确认为编译选项，修改 Makefile → 关闭
- E2 仍 <1.0x vs 标量 → 根因在代码逻辑差异 → 进入 E4/代码修正

---

## P1 — 高优先级（Phase 1 收尾前完成）

| 编号 | 任务 | 类型 | 负责 | 关联决议 | 预估 |
|------|------|------|------|----------|------|
| TODO-05 | INVESTIGATION 报告补充 §7.1 不确定度声明 + §1 增加 POC 对比行 | 文档 | Algorithm | D-034, D-040 | 10 min | ✅ 已完成 |
| TODO-06 | `platform_cpu.c` 头部增加注释 | 开发 | Backend | D-035 | 5 min | ✅ 已完成 |
| TODO-07 | Benchmark 报告补充 N=5 重复测量 ±σ | 测试 | Backend | A-01 | 20 min | ✅ 已完成 |

---

## P2 — 中优先级（Phase 2 前完成）

| 编号 | 任务 | 类型 | 负责 | 关联决议 | 预估 | 状态 |
|------|------|------|------|----------|------|------|
| TODO-08 | `search_avx2.c` 循环内添加防御性注释（`block = hi - 8` 依赖注明） | 开发 | Security | D-039, SEC-01 | 5 min | ✅ 已完成 |
| TODO-09 | CI 增加 `INT32SEARCH_FORCE_AVX2` 编译配置 | 配置 | Security | D-039, SEC-03 | 20 min | ✅ 已完成 |
| TODO-10 | 补充 `objdump -d` 完整反汇编分析（序言/结语/YMM 溢出） | 测试 | Security | D-039, 盲区1 | 15 min | ✅ 已完成 |

> **P2 详细完成记录** → [05_completion_report/](05_completion_report/)
> - [TODO-08 防御性注释](05_completion_report/todo08_defensive_comment.md)
> - [TODO-09 FORCE_AVX2 配置](05_completion_report/todo09_force_avx2_config.md)
> - [TODO-10 objdump 反汇编](05_completion_report/todo10_objdump_analysis.md)

---

## P3 — 低优先级（不阻塞任何交付）

| 编号 | 任务 | 类型 | 负责 | 关联决议 | 预估 | 状态 |
|------|------|------|------|----------|------|------|
| TODO-11 | Linux 环境拉取相同代码，运行 4 组 benchmark（原 FIXPLAN 第四波，已降级） | 测试 | Backend | D-037 | 1 h | ✅ 已完成 |
| TODO-12 | API else 分支（标量回退）在 AVX2 机器上的覆盖测试 | 测试 | Security | SEC-02 | 15 min | ✅ 已完成 |

> **P3 详细完成记录** → [05_completion_report/](05_completion_report/)
> - [TODO-11 Linux Benchmark](05_completion_report/todo11_linux_benchmark.md)
> - [TODO-12 标量回退覆盖测试](05_completion_report/todo12_scalar_fallback.md)

---

## 安全跟踪表（D-039 决议）

| 编号 | 描述 | 严重程度 | 对应 TODO |
|------|------|----------|-----------|
| SEC-01 | `block = hi - 8` 的脆性依赖（依赖 while 条件） | Medium | TODO-08 ✅ |
| SEC-02 | API else 分支未在 AVX2 机器上覆盖 | Minor | TODO-12 ✅ |
| SEC-03 | AVX2 代码腐烂风险（默认不走 AVX2 时） | Minor | TODO-09 ✅ |
| SEC-04 | `__builtin_cpu_supports` 假阳性 #UD 崩溃风险 | Medium | DEEP-04 ✅（[评估报告](../../../tasks/task_001_phase1_mvp/ASSESSMENT_cpu_supports_false_positive_task_001_phase1_mvp.md)） |

---

## TODO-02 详细说明：构建时函数指针方案

**涉及文件**：

1. `src/internal.h` — 在 `int32_search_impl_t` 中增加字段：
   ```c
   int32_t (*search_fn)(const int32_t *vals, size_t n, int32_t key, size_t *out_index);
   uint8_t   avx2_capable;
   ```
   新增宏：`#define INT32_SEARCH_AVX2_MIN_N 10000000ULL`

2. `src/api.c` — `int32_search_create()` 末尾增加：
   ```c
   impl->avx2_capable = (uint8_t)platform_cpu_has_avx2();
   if (impl->avx2_capable && impl->n > INT32_SEARCH_AVX2_MIN_N) {
       impl->search_fn = search_avx2_find;
   } else {
       impl->search_fn = search_scalar_find;
   }
   ```

3. `src/api.c` — `int32_search_find()` 简化：
   ```c
   // 原: if (impl->path == PATH_A)
   //        platform_cpu_has_avx2() ? search_avx2_find(...) : search_scalar_find(...)
   // 新: return impl->search_fn(impl->vals, impl->n, key, out_index);
   ```

**约束**：
- `search_fn` 签名不变：`(const int32_t *vals, size_t n, int32_t key, size_t *out_index)`
- 不传 `impl` 进去（保持分层隔离）
- Phase 2 兼容：B1 路径就绪后直接替换 `search_fn` 赋值即可

---

## 文档更新清单

| 文档 | 更新内容 | 触发 TODO |
|------|----------|-----------|
| `INVESTIGATION_windows_avx2_task_001_phase1_mvp.md` | §7.1 不确定度声明 + §1 POC 对比行 | TODO-05 |
| `FINAL_task_001_phase1_mvp.md` | 新增"已知平台限制"条目 | TODO-04 |
| `技术路线.md` §1 | D-008 修订（加平台限定词） | TODO-03 |
| `技术路线.md` §7 | 新增 AVX2 MinGW 退化风险条目 | TODO-03 |
| `src/platform_cpu.c` | 头部注释 | TODO-06 |
| `src/internal.h` | `search_fn` + `avx2_capable` 字段 + 宏 | TODO-02 |
| `src/api.c` | create() 函数指派 + find() 简化 | TODO-02 |

---

> **本次会议所有待办事项已记录。共 12 项 TODO + 4 项 SEC 跟踪。**
> **P0: 4/4 ✅ | P1: 3/3 ✅ | P2: 3/3 ✅ | P3: 2/2 ✅ — 全部完成**
> **SEC: 4/4 ✅ — 全部关闭（SEC-04 DEEP-04 评估报告已交付）**
> **会议状态：全部待办事项清理完毕，可进入 Frozen。**
