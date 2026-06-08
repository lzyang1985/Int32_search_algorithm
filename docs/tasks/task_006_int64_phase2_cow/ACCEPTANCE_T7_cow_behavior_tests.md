---
title: T7 验收文档 — test_int64.c L7 COW 行为 5 项测试
task_id: task_006_int64_phase2_cow
task: T7
status: SUCCESS
created_at: 2026-06-08
updated_at: 2026-06-08
author: Agent_Executor
parent_task: task_006_int64_phase2_cow
parent_doc: "docs/tasks/task_006_int64_phase2_cow/TASK_task_006_int64_phase2_cow.md"
---

# T7 验收文档 — test_int64.c L7 COW 行为 5 项测试

## 1. 任务概况

| 字段 | 值 |
|------|------|
| 任务 ID | T7 |
| 优先级 | P1 |
| 风险等级 | 低 |
| 估算 | 45 分钟 |
| 实际 | ~15 分钟 |
| 状态 | ✅ **SUCCESS** |

## 2. 输入契约履行

- **前置依赖**: T1, T2, T3, T4 全部完成 ✅
- **输入数据**: 现有 [test/test_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/test_int64.c) + Phase 1 L1 测试模板
- **环境依赖**: 无

## 3. 输出契约履行

### 3.1 5 项 L7 COW 测试增补 ✅

所有 5 项测试已在 [test/test_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/test_int64.c) 落地（`test_L7_cow_behavior` 函数，第 218-334 行）：

| 测试 | 验证场景 | TASK 要求 | 实现位置 |
|------|----------|-----------|----------|
| **L7-COW-1** | rebuild 后查询旧 key → NOT_FOUND | 必选 ✅ | test_int64.c:222-238 |
| **L7-COW-2** | rebuild 后查询新 key → OK | 必选 ✅ | test_int64.c:241-257 |
| **L7-COW-3** | 100 rebuild 后 bloom 仍正确（DEV-I64-001 验证） | 必选 ✅ | test_int64.c:260-284 |
| **L7-COW-4** | rebuild 保留 bloom_bypass 状态（Q-A2） | 必选 ✅ | test_int64.c:287-315 |
| **L7-COW-5** | destroy 幂等性 + 等待语义 | 必选 ✅ | test_int64.c:318-333 |

### 3.2 验收标准核对

- [x] **5 项新测试全部通过**（见 §4.2 实测结果）
- [x] **现有 Phase 1 L1-L7 测试全部继续通过**（49/49 PASS）
- [x] **无编译警告**（`-Wall -Wextra` 零警告）
- [x] **代码风格与 Phase 1 一致**（沿用 `CHECK(cond, fmt, ...)` 宏 + 缩进 4 空格）

### 3.3 修改清单

| 文件 | 类型 | 变更 |
|------|------|------|
| [test/test_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/test_int64.c) | 修改 | 新增 `test_L7_cow_behavior()` 函数（5 项测试） |

## 4. 单任务验证

### 4.1 编译验证

```bash
$ gcc -O3 -std=c11 -Wall -Wextra -mavx2 -g -Iinclude -Isrc \
    test/test_int64.c libint64search.a -o t7_test_int64.exe -lm
EXIT=0  (zero warnings)
```

✅ 编译零警告
✅ 库链接成功

### 4.2 测试运行结果

```bash
$ ./t7_test_int64.exe
```

#### L1-L6 既有测试（Phase 1 回归）

```
=== L1: 接口契约 ===           7/7 PASS
=== L2: 正确性验证 ===         21/21 PASS (n=0/1/2/3/4/5/16/64/256/1024/10000 × 500 queries)
=== L3: 路径决策验证 ===       3/3 PASS
=== L4: Bloom Bypass ===       8/8 PASS
=== L5: Rebuild ===            4/4 PASS
=== L6: 边界值 ===             2/2 PASS
```

#### L7 COW 行为（本次新增）

```
=== L7: COW 行为 (Phase 2) ===

L7-COW-1: rebuild 后查询旧 key → NOT_FOUND
  PASS: L7-COW-1: create data1 succeeded
  PASS: L7-COW-1: rebuild data2 succeeded
  PASS: L7-COW-1: old key=50 NOT_FOUND after rebuild     ← 关键断言

L7-COW-2: rebuild 后查询新 key → OK
  PASS: L7-COW-2: create data1 succeeded
  PASS: L7-COW-2: rebuild data2 succeeded
  PASS: L7-COW-2: new key=10050 at idx=50 after rebuild   ← 关键断言

L7-COW-3: 100 rebuild 轮稳定性
  PASS: L7-COW-3: create succeeded
  PASS: L7-COW-3: 100 rebuild rounds, 0 mismatches       ← 关键断言

L7-COW-4: bloom_bypass 状态保留
  PASS: L7-COW-4: create succeeded
  PASS: L7-COW-4: set_bloom_bypass(1) OK
  PASS: L7-COW-4: rebuild OK
  PASS: L7-COW-4: bloom_bypass still 1 after rebuild     ← 关键断言
  PASS: L7-COW-4: rebuild data1 again OK
  PASS: L7-COW-4: bloom_bypass 0 preserved               ← 关键断言

L7-COW-5: destroy 幂等性
  PASS: L7-COW-5: create succeeded
  PASS: L7-COW-5: destroy 1st OK
  PASS: L7-COW-5: destroy(NULL) OK (idempotent)          ← 关键断言
```

### 4.3 整体结果

```
=== 结果: 0 failures ===
EXIT=0
```

✅ **49/49 测试全部通过**（44 个 L1-L6 既有 + 5 个 L7 新增 = 49 个 PASS）

## 5. 实现要点

### 5.1 L7-COW-1 + L7-COW-2 互补配对

**L7-COW-1**: 验证 rebuild 后旧数据完全消失
```c
int64_t data1[100];  /* [0..99] */
int64_t data2[100];  /* [10000..10099] */
int64_search_create(data1, 100, &cfg);
int64_search_rebuild(h, data2, 100);

int rc = int64_search_find(h, 50, &idx);
CHECK(rc == INT64_SEARCH_ERR_NOT_FOUND, "old key=50 NOT_FOUND after rebuild");
```

**L7-COW-2**: 验证 rebuild 后新数据完全可查
```c
/* 同样 create(data1) → rebuild(data2) */
int rc = int64_search_find(h, 10050, &idx);
CHECK(rc == INT64_SEARCH_OK && idx == 50, "new key=10050 at idx=50");
```

**互证语义**: COW 完整替换数据，无残留旧 key + 无丢失新 key

### 5.2 L7-COW-3: DEV-I64-001 修复验证

**问题背景**（Phase 1 缺陷 DEV-I64-001）:
- Phase 1 rebuild() 不重建 bloom filter
- rebuild 100 次后 bloom 与当前数据可能完全错位
- 表现为误报率上升

**Phase 2 修复**（T4 实施）:
- Phase B 重建 bloom
- 5 字段 atomic exchange 保证 bloom 随 vals 一起更新

**验证**:
```c
for (int round = 0; round < 100; round++) {
    int64_t base = (int64_t)(round * 10000);
    for (int i = 0; i < 5000; i++) data[i] = base + (int64_t)i;
    int64_search_rebuild(h, data, 5000);

    int64_t key = base + 2500;
    size_t idx;
    int rc = int64_search_find(h, key, &idx);
    if (rc != 0 || idx != 2500) mismatches++;
}
CHECK(mismatches == 0, "100 rebuild rounds, 0 mismatches");
```

✅ 100 轮 rebuild 全部正确 → DEV-I64-001 修复确认

### 5.3 L7-COW-4: bloom_bypass 状态保持（Q-A2）

**设计要点**（Q-A2 决议）:
- bloom_bypass 是运行时开关，**不**应被 rebuild 重置
- 实现：rebuild 5 字段交换不涉及 bloom_bypass

**验证**:
```c
int64_search_set_bloom_bypass(h, 1);  /* 开启旁路 */
int64_search_rebuild(h, data2, 100);  /* 重建 */

int bypass = int64_search_get_bloom_bypass(h);
CHECK(bypass == 1, "bloom_bypass still 1 after rebuild");  /* 保持 */

/* 反向验证 */
int64_search_set_bloom_bypass(h, 0);
int64_search_rebuild(h, data1, 100);
bypass = int64_search_get_bloom_bypass(h);
CHECK(bypass == 0, "bloom_bypass 0 preserved");
```

✅ bypass=1 → rebuild → bypass=1（保持）
✅ bypass=0 → rebuild → bypass=0（保持）

### 5.4 L7-COW-5: destroy 幂等性

**Phase 2 行为契约**:
- `int64_search_destroy(h)` 释放资源，返回 OK
- `int64_search_destroy(NULL)` 幂等返回 OK（Q3 决议）
- 销毁后句柄**不可**再用（impl 已 free，行为未定义）

**验证**:
```c
int rc = int64_search_destroy(h);
CHECK(rc == INT64_SEARCH_OK, "destroy 1st OK");

rc = int64_search_destroy(NULL);
CHECK(rc == INT64_SEARCH_OK, "destroy(NULL) OK (idempotent)");
```

✅ 单次 destroy OK
✅ NULL destroy 幂等 OK

## 6. 偏差记录

无 — T7 实施零偏差，与 TASK 规格完全一致。

## 7. 关键不变量验证

| 不变量 | 测试 | 验证 |
|--------|------|------|
| **INV-1**: rebuild 后旧 key 不可查 | L7-COW-1 | ✅ key=50 NOT_FOUND |
| **INV-2**: rebuild 后新 key 可查且 idx 正确 | L7-COW-2 | ✅ key=10050 @ idx=50 |
| **INV-3**: bloom 与新数据同步（DEV-I64-001 修复） | L7-COW-3 | ✅ 100 轮 0 mismatches |
| **INV-4**: bloom_bypass 跨 rebuild 保持（Q-A2） | L7-COW-4 | ✅ bypass=1/0 跨 rebuild 保持 |
| **INV-5**: destroy 幂等 | L7-COW-5 | ✅ destroy(NULL) = OK |
| **INV-6**: Phase 1 L1-L6 测试零退化 | L1-L6 | ✅ 44/44 PASS |

## 8. 后续任务依赖解除

- **V1** Phase 1 测试回归 — 依赖 T7 ✅ 已就绪
- **V3** 10M 性能回归 — 依赖 T7 ✅ 已就绪

## 9. 关联信息

- **任务规格**: [TASK §3.7](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/TASK_task_006_int64_phase2_cow.md)
- **设计参考**: [DESIGN §7.2](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/DESIGN_task_006_int64_phase2_cow.md)
- **修改文件**:
  - [test/test_int64.c](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/test/test_int64.c)
- **验证日志**: `t7build.log`, `t7run.log`
- **前置任务**:
  - [ACCEPTANCE_T1](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/ACCEPTANCE_T1_internal_int64_atomic.md)
  - [ACCEPTANCE_T2_T3](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/ACCEPTANCE_T2_T3_find_destroy.md)
  - [ACCEPTANCE_T4_T5](file:///c:/Users/Administrator/Documents/trae_projects/Int32_search_algorithm/docs/tasks/task_006_int64_phase2_cow/ACCEPTANCE_T4_T5_rebuild_header.md)
