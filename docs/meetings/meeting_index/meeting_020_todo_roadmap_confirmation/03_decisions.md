---
title: 会议决议 — 剩余待办事项收尾路线确认会
meeting_id: meeting_020_todo_roadmap_confirmation
status: Reviewing
created_at: 2026-06-10
updated_at: 2026-06-10
author: Agent_Executor
parent_doc: docs/meetings/meeting_index.md
source_docs:
  - "02_discussion.md"
---

# 会议决议

## D-165：28项行动项→16项唯一项，去重完成

28项跨会议重复计数经去重后为 **16 项唯一行动项**。后续所有追踪以 U-01~U-16 编号为准。5/5 通过。

---

## D-166：V4 概念正式消解

meeting_019 D-156/D-157/D-158 三连修后，D-140/D-142 均默认关闭，V1 和 V4 编译产物完全一致。V4 概念正式废弃，bench_100.ps1 移除 V4 对比逻辑，改为单版本 N 次统计。5/5 通过。

---

## D-167：收尾路线 — Phase A' 精简版

以下为本次会议确定的收尾路线：

```
Phase A'（安全收尾 + 工程基础，≤ 2 天）：
┌─────────────────────────────────────────────────────────────┐
│ P0:  U-01  Int64 D-143 防御移植          30min   Sec       │
│ P1:  U-06  Int64 D-142 小桶移植           30min   Algo      │
│ P1:  U-07  Dir fuzz 基础覆盖率             1天    Sec       │
│ P1:  U-10  bench_100.ps1 方法论增强        1h     Fullstack │
│ P1:  U-02  64B 对齐（独立）               5min    Backend   │
│ P1:  U-03  Huge Pages 快速验证(Linux)      2h     Algo      │
│ P1:  U-02  PGO Makefile target             1天    Backend   │
│ P1:  U-08  find_with_hint 最小原型         2天    Fullstack │
└─────────────────────────────────────────────────────────────┘

Phase B（Gate 后条件触发）：
┌─────────────────────────────────────────────────────────────┐
│ P1:  U-04  预取调优 Stage 0              0.5天   Algo      │
│            L3 miss <5% → 直接关闭                             │
│            L3 miss ≥5% → Stage 1 (1天)                       │
│ P1:  U-05  热键缓存埋点                     2天    Algo      │
│ P2:  U-11  D-140 重验证(PGO后,30min时间盒) 30min   Algo      │
│ P2:  U-02  LTO Makefile opt-in target     0.5天   Backend   │
│ P2:  U-13  DX收尾（README + CI扩展）      0.5天   Fullstack │
└─────────────────────────────────────────────────────────────┘

直接关闭：
┌─────────────────────────────────────────────────────────────┐
│ ✗ U-09  编译器flag实验  → 合并到 U-02 PGO 工作中             │
│ ✗ U-12  热键缓存完整实现 → 条件：埋点收益 > 1.8x             │
│ ✗ U-14  COW seqlock调研 → [YAGNI-Seqlock]                   │
│ ✗ U-15  find_with_hint API调研 → 已被 U-08 覆盖              │
│ ✗ U-16  批量查询API → [YAGNI-BatchAPI]                      │
└─────────────────────────────────────────────────────────────┘
```

5/5 通过。

---

## D-168：维护模式门禁修订 — G6-minimal

原 D-138/D-139 将 G6 设为"性能 POC 完成"，属于过度设计。修订为：

```
G6-minimal = G1 + G2 + G3 + G4 + G5 + SG-1 + SG-2 + SG-3 + SG-4
           = Int32/Int64 功能完整
           + CI/CD 基础版
           + TODO P1 闭合
           + MinGW 退化定论
           + 安全四门禁
```

**不含** D-130~D-133b 性能 POC。维护模式的核心语义是"功能完整 + 安全闭合 + 可维护"，不是"性能榨干"。5/5 通过。

---

## D-169：task_006 立即归档

task_006 Int64 Phase2 COW 满足全部归档条件：11/11 SUCCESS、10 份文档 Frozen、双平台验证通过、ASan/UBSan/TSan 零告警。立即执行归档，在 FINAL 文档中标注 U-01 为 post-archive 安全修复。5/5 通过。

---

## D-170：安全 P0 — U-01 Int64 D-143 立即执行

当前 SG-2 门禁不通过，阻塞维护模式准入。ACT-41/ACT-58 合并为 U-01，立即执行（30min）：在 `search_b1_int64.c` 中追加 D-143-minimal 等效防御 `if ((size_t)end > n) return (size_t)-1;`。5/5 通过。

---

## D-171：CMakeLists.txt 删除

CMakeLists.txt 严重过时（缺 Int64 全部目标），双构建系统不同步是技术债务。用户习惯 gcc + Makefile + README.txt。删除 CMakeLists.txt，在 README.txt 中标注"CMake support is community-maintained; the authoritative build system is Makefile + gcc"。5/5 通过。

---

## D-172：Rust RAII 示例取消

无实际需求信号，维护负担 > 价值。改为在 README.txt 中提供 C 语言 API 使用示例。5/5 通过。

---

## D-173：关闭项清单（正式关闭，不再追踪）

| U-ID | 行动项 | 关闭标注 |
|------|--------|----------|
| U-04 | 预取调优（全量） | 改为分阶段 Gate 推进，原始 P1 关闭 |
| U-09 | 编译器flag实验 | 合并到 U-02 PGO 工作中 |
| U-12 | 热键缓存完整实现 | 条件：U-05 埋点收益 > 1.8x |
| U-14 | COW seqlock调研 | `[YAGNI-Seqlock]` |
| U-15 | find_with_hint API调研 | 已被 U-08 覆盖 |
| U-16 | 批量查询API | `[YAGNI-BatchAPI]` |

5/5 通过。

---

## D-174：会议 016/017/018 待 Frozen

Phase A' 完成后，meeting_016、meeting_017、meeting_018 全部 Frozen。meeting_019 已在会前 Frozen。5/5 通过。
