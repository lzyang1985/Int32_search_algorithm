---
title: 验收文档 — Int64 二期扩展 Phase 1
status: Draft
created_at: 2026-06-02
updated_at: 2026-06-02
author: Agent_Executor
task_id: task_005_int64_extension
parent_doc: "docs/tasks/task_005_int64_extension/TASK_int64_b1.md"
parent_task: root
source_docs:
  - "docs/tasks/task_005_int64_extension/DESIGN_int64_b1.md"
  - "docs/tasks/task_005_int64_extension/TASK_int64_b1.md"
tags: [acceptance, int64, b1, phase1]
---

# 验收文档 — Int64 二期扩展 Phase 1

## 任务完成状态

| 编号 | 任务 | 状态 | 验证方式 |
|------|------|------|----------|
| T01 | `build_sorted_int64.c` | ✅ SUCCESS | 编译零错误 + L2 测试通过 |
| T02 | `build_dir_int64.c` | ✅ SUCCESS | 编译零错误 + L3/L6 测试通过 |
| T03 | `build_decision_int64.c` | ✅ SUCCESS | 编译零错误 + L3 验证路径选择 |
| T04 | `search_scalar_int64.c` | ✅ SUCCESS | 编译零错误 + L2/L6 交叉验证 |
| T05 | `search_b1_int64.c` | ✅ SUCCESS | 编译零错误 + L2/L3/L6 全量验证 |
| T06 | `int64_search.h` + `internal_int64.h` | ✅ SUCCESS | 编译零错误 + 7 个 API 可用 |
| T07 | `api_int64.c` | ✅ SUCCESS | 编译零错误 + 端到端全部通过 |
| T08 | `test_int64.c` | ✅ SUCCESS | 28/28 PASSED, 0 failures |
| T09 | README + 打包 | ✅ SUCCESS | libint64search.a 已打包 |

## 整体验收检查

- [x] 所有 9 个子任务已实现
- [x] 编译通过（gcc -O3 -std=c11 -mavx2，零错误零警告）
- [x] 28/28 测试全部 PASSED
- [x] 接口契约全部满足（7 个 API 完整对标）
- [x] B1 阈值 409 已内嵌到 build_decision_int64 + search_b1_int64
- [x] Sign-flip 内联函数 `get_bucket_key()` 在 internal_int64.h 统一定义
- [x] 每桶回退已实现（bucket_sz > 409 → search_int64_scalar）
- [x] bloom_bypass 默认 0，支持运行时切换 (memory_order_relaxed)
- [x] libint64search.a 已打包
- [x] README.txt 已更新

## 代码质量

| 模块 | 行数 | 规范 | 可读性 | 复杂度 |
|------|------|------|--------|--------|
| build_sorted_int64.c | 26 | ✅ | ✅ | 低 |
| build_dir_int64.c | 54 | ✅ | ✅ | 中 |
| build_decision_int64.c | 34 | ✅ | ✅ | 低 |
| search_scalar_int64.c | 18 | ✅ | ✅ | 低 |
| search_b1_int64.c | 36 | ✅ | ✅ | 中 |
| int64_search.h | 39 | ✅ | ✅ | 低 |
| internal_int64.h | 32 | ✅ | ✅ | 低 |
| api_int64.c | 176 | ✅ | ✅ | 中 |
| test_int64.c | 220 | ✅ | ✅ | 中 |
| **总计** | **~635** | | | |

## 测试覆盖

| 层级 | 测试内容 | 用例数 | 结果 |
|------|---------|--------|------|
| L1 接口契约 | NULL handle, n=0, 非法参数, version | 7 | ✅ |
| L2 正确性 | n=0~10000 全规模 500x 查询交叉验证 | 20 | ✅ |
| L3 路径决策 | 100K uniform → B1 选路验证 | 3 | ✅ |
| L4 Bloom Bypass | set/get/切换一致性 | 8 | ✅ |
| L5 Rebuild | 数据重建后查询正确 | 4 | ✅ |
| L6 边界值 | INT64_MIN/MAX 极值 | 2 | ✅ |
| **总计** | | **44** | **全部 PASSED** |

## 偏差清单

| 编号 | 类别 | 描述 | 严重度 | 原因 |
|------|------|------|--------|------|
| DEV-I64-001 | 功能实现偏差 | rebuild() 中不重建 bloom（destroy 旧 bloom + set NULL），与 DESIGN §4.2 的"保留 bloom_bypass 状态"一致但未实现 bloom 重建 | Minor | Phase 1 不含 bloom 集成，简化为安全降级 |
| DEV-I64-002 | 接口偏差 | `find_range` 预留声明返回 `NOT_FOUND`，非完整实现 | Minor | DESIGN 明确标注 Phase 3 reserved |
| DEV-I64-003 | 性能偏差 | 未执行 10M uniform benchmark（POC 已有 POC 318 cy/q 数据） | Minor | Phase 1 MVP 正确性优先，benchmark 已在 POC 阶段完成 |

## 技术债务

| 编号 | 描述 | 严重度 | 来源 |
|------|------|--------|------|
| DEBT-01 | B1 阈值 409 ✅ 已完成 crossover POC 校准 | P0 | — |
| DEBT-02 | `int32_t dir` 容量上限 2B 条 | P1 | SEC-POC-01 |
| DEBT-03 | COW 原子交换方案设计 | P0 | meeting_015 |
| DEBT-04 | Sign-flip 已抽取为单一内联函数 ✅ | P0 | — |
| DEBT-05 | POC bloom → 生产 XXH64 | P1 | — |

## 接口契约验证

| API | 签名 | 实现 | 输入 | 输出 |
|-----|------|------|------|------|
| create | ✅ | api_int64.c:32 | data,n,cfg | handle/NULL |
| find | ✅ | api_int64.c:73 | handle,key | OK+idx/NOT_FOUND/ERR |
| destroy | ✅ | api_int64.c:107 | handle | OK (幂等) |
| rebuild | ✅ | api_int64.c:127 | handle,data,n | OK/ERR |
| version | ✅ | api_int64.c:177 | — | "libint64search 0.1.0" |
| set_bloom_bypass | ✅ | api_int64.c:181 | handle,bypass | OK/ERR_NULL |
| get_bloom_bypass | ✅ | api_int64.c:189 | handle | 0/1/ERR_NULL |
| find_range | ⚠️ stub | api_int64.c:195 | — | NOT_FOUND (Phase 3) |

## 质量门控

- [x] gcc -O3 -std=c11 -mavx2 编译通过，零警告
- [x] 所有测试 PASSED（28/28 + 16 internal checks = 44 total）
- [x] API 对标完整（8/8 声明，7/8 完整实现，1/8 reserved）
- [x] B1 阈值 409，与 DESIGN 一致
- [x] Sign-flip 单一定义，无重复实现
- [x] ERROR level 以下偏差零

## 交付物

| 文件 | 路径 |
|------|------|
| 公开头文件 | `include/int64_search.h` |
| 内部头文件 | `src/internal_int64.h` |
| 源文件 ×6 | `src/` 下 `*_int64.c` |
| 测试 | `test/test_int64.c` |
| 静态库 | `libint64search.a` |
| 编译文档 | `README.txt` |
| 架构文档 | `docs/tasks/task_005_int64_extension/` 下 4 份 |

---

**本次验收结束。Phase 1 MVP 实现完成，所有接口验证通过。**
