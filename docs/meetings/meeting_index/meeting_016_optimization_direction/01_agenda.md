---
title: 优化方向讨论会议程
meeting_id: meeting_016_optimization_direction
status: Draft
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
parent_doc: docs/meetings/meeting_index/meeting_016_optimization_direction/meeting_README.md
parent_task: root
---

# 优化方向讨论会议程

## 会议背景

本项目 (libint32search / libint64search) 已历经四阶段交付：

| 阶段 | 版本 | 内容 | 状态 |
|------|------|------|------|
| Phase 1 | MVP | AVX2 Path A 单路径 | ✅ Frozen |
| Phase 1.5 | v0.2 | Path A COW 多线程 | ✅ Frozen |
| Phase 2 | v1.0 | A+B1 双路径 + B1 COW | ✅ Frozen |
| Phase 3 | v1.1 | find_range + bloom_filter + Windows yield | ✅ Frozen |
| Phase 4 | Int64 v0.1.0 | Int64 B1 路径 + Bloom Bypass | ✅ Frozen |

当前有 15 项已知 TODO（参见 `docs/tasks/task_005_int64_extension/TODO_int64_b1.md`），涵盖修复/优化/配置/测试和远期方向。

## 议题

### 1. 当前待办收尾 — 是否还有必须修复的阻塞项？

> 来源：`TODO_int64_b1.md` 的 15 项待办

- **Fix**: TODO-01 (unused variable warning)
- **Optimization**: TODO-02 (溢出检查)、TODO-03 (SIMD broadcast 提升)
- **Config**: TODO-04 (Makefile int64 target)、TODO-05 (.gitignore)
- **Test**: TODO-06~09 (10M 回归、zipf 退化、SIMD mask 快速路径、rebuild bloom 状态)
- **Phase 2 前置**: TODO-10 (COW 原子交换方案)、TODO-11 (bloom 重建)、TODO-12 (int64_t dir)
- **Phase 3 远期**: TODO-13 (find_range B1)、TODO-14 (Eytzinger)、TODO-15 (AVX-512)

### 2. Int64 二期的优先级 — 是继续投入还是暂缓？

目前 Int64 v0.1.0（B1 路径 + Bloom Bypass）已交付，但以下能力尚缺：

- COW 多线程（TODO-10，x86-64 超出 16 字节 lock-free 上限需 spinlock）
- Bloom 重建逻辑（TODO-11）
- find_range 在 B1 路径的实现（TODO-13）
- dir 从 int32_t → int64_t 迁移（TODO-12，支持 n > 2^31）

**问题**: Int64 这些二期特性是否有实际业务需求支撑？还是可以暂缓到有明确用例时再推进？

### 3. Int32 平台性能差距 — MinGW AVX2 退化问题

当前已知限制（README §10）：
> Windows MinGW 下 AVX2 Path A 性能退化 (0.45x-0.55x vs Linux 5.26x)

Path B1 和 Path Scalar 不受影响，但 Path A 在 Windows 下实质不可用。

**讨论方向**:
- 是否有必要投入调研 MinGW 编译器优化的替代方案（如 Clang for Windows）？
- 还是接受"Windows 用 B1/标量，Linux 用全路径"的现状？

### 4. 指令集拓展 — AVX-512 和 ARM NEON

- **AVX-512**: 技术路线中已预留，TODO-15 列为 Phase 3 远期。但 AVX-512 在高频降频（frequency throttling）问题上存在争议。
- **ARM NEON**: 未在任何文档中出现。ARM 服务器市场份额上升，是否有必要考虑？

### 5. 算法层面的进一步探索

当前最优方案是 B1 路径（high-N-bit dir + lo 段扫描），适合均匀分布数据。以下方向是否值得探索：

- **Eytzinger 布局** (TODO-14)：分支预测友好的内存布局，在标量二分上可能有额外提升
- **适应倾斜分布的新路径**：当前倾斜数据回退到 Path A 或标量二分，没有任何针对倾斜分布的优化
- **RMI (Recursive Model Index)**：学术界近年提出的学习型索引，在整数领域有不错表现

### 6. 工程质量 — 还有什么改进空间？

- Makefile 扩展（TODO-04 int64 target）
- CI/CD 流水线（当前无）
- 更好的 Benchmark 可视化
- 更完整的跨编译器测试矩阵（GCC/Clang/MSVC）

### 7. 项目的终局 — 定义"完成"标准

本项目从 2026-05-27 启动，历经 15 次会议。需要定义什么是"项目停止维护"或"达到里程碑"的状态。

**问题**: 项目是否可以标记为 **"Feature Complete + 维护模式"** ？如果还有「必须做」的事，是什么？
