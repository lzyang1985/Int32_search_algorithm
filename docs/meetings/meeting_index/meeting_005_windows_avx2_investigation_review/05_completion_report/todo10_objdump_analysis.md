---
title: TODO-10 完成记录 — objdump 完整反汇编分析
status: Frozen
created_at: 2026-05-30
updated_at: 2026-05-30
author: Agent_Executor
meeting_id: meeting_005_windows_avx2_investigation_review
parent_doc: "docs/meetings/meeting_index/meeting_005_windows_avx2_investigation_review/04_action_items.md"
task_ref: TODO-10
resolution: D-039 (盲区1)
tags: [objdump, disassembly, avx2, YMM, vzeroupper]
---

# TODO-10 objdump 完整反汇编分析

**环境**：Linux 5.15.0-30, x86_64, GCC 11.4.0, `-O3 -mavx2`

## 序言

```
endbr64                        ; CET landing pad
test rsi,rsi; je error        ; n==0 提前退出
test rdi,rdi; je error        ; vals==NULL 提前退出
push rbp; mov rbp,rsp         ; 帧指针
push rbx                       ; 仅保存 rbx
vmovd xmm1,r8d                 ; target → xmm1
vpbroadcastd ymm1,xmm1        ; 广播到 8 路
```

栈帧仅 8 字节（rbx），无 YMM 寄存器保存（leaf function，ymm0~ymm15 为 caller-saved）。

## 循环体核心（4 条 YMM 指令）

```
vmovdqu ymm2,[rdi+rax*4]       ; _mm256_loadu_si256
vpcmpgtd ymm0,ymm2,ymm1        ; _mm256_cmpgt_epi32
vmovmskps r10d,ymm0            ; _mm256_movemask_ps
popcnt r10d,r10d                ; __builtin_popcount
```

## 结语 — 3 条返回路径均有 vzeroupper

```
(成功+out_index)  vzeroupper → mov rbx,[rbp-8] → leave → ret
(成功)            vzeroupper → mov rbx,[rbp-8] → leave → ret
(NOT_FOUND)       mov eax,-1 → vzeroupper → jmp restore → ret
```

## 检查清单

| 检查项 | 结果 |
|--------|------|
| YMM 溢出（spill） | ✅ **零溢出** |
| `vzeroupper` 覆盖 | ✅ **3/3 出口均覆盖** |
| 栈帧大小 | ✅ 仅 8 字节（rbx） |
| `.text` 段大小 | 415 bytes，完全适配 L1 指令缓存 |
| 条件分支 | ✅ `cmovb`/`cmova` 替代分支跳转 |
| `vmovdqu` vs `vmovdqa` | ✅ 非对齐加载（与源码一致） |

**结论**：代码生成质量优秀，无 YMM 溢出、无缺失 `vzeroupper`、无冗余栈操作。
