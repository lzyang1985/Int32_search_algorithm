---
title: 行动项 — Int64 扩展 + Bloom 旁路 POC 设计
meeting_id: meeting_014_poc_design
status: Frozen
created_at: 2026-06-02
updated_at: 2026-06-02 (ACT-14~18 all completed)
author: Agent_Executor
---

# 行动项 — Int64 扩展 + Bloom 旁路 POC 设计会议

## 📋 行动项总览

| 编号 | 行动项 | 优先级 | 依赖 | 状态 |
|------|--------|--------|------|------|
| ACT-14 | 创建 poc_int64_avx2.c — Path A 4 路 SIMD 二分验证 | P0 | 无 | ✅ Completed |
| ACT-15 | 创建 poc_int64_b1.c — Path B1 high20 dir + lo44 scan | P1 | ACT-14 | ✅ Completed |
| ACT-16 | 创建 poc_bloom_bypass.c — Bloom 旁路正确性验证 | P1 | 无 | ✅ Completed |
| ACT-17 | 更新 README.txt — POC 编译命令 + 运行说明 | P0 | ACT-14 | ✅ Completed |
| ACT-18 | POC 结果汇总 + Go/No-Go 报告 | P1 | ACT-14, ACT-15, ACT-16 | ✅ Completed |

---

## 行动项详情

### ACT-14：实现 poc_int64_avx2.c [P0]

**目标**：验证 int64 Path A AVX2 4 路 SIMD 二分的功能正确性和基础性能。

**交付物**：`src/poc_int64_avx2.c`（单文件自包含）

**核心算法**（对标 D-103 规范）：

```c
/* int64 4 路 SIMD 二分查找 */
size_t search_int64_avx2(const int64_t *vals, size_t n, int64_t target) {
    if (n == 0) return SIZE_MAX;
    size_t lo = 0, hi = n;

    while (hi - lo >= 4) {
        size_t mid = lo + (hi - lo) / 2;
        size_t block = mid & ~(size_t)3;
        if (block < lo) block = lo;
        if (block + 4 > hi) block = hi - 4;

        __m256i key = _mm256_set1_epi64x(target);
        __m256i vec = _mm256_loadu_si256((const __m256i *)(vals + block));
        __m256i cmp = _mm256_cmpgt_epi64(vec, key);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(cmp));
        int le_count = 4 - __builtin_popcount((unsigned)mask);

        if (le_count == 0) {
            hi = block;
        } else {
            size_t last_le = block + (size_t)le_count - 1;
            if (vals[last_le] < target) {
                lo = block + (size_t)le_count;
            } else {
                if (block + (size_t)le_count == hi) break;
                hi = block + (size_t)le_count;
            }
        }
    }

    /* 标量收尾 */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (vals[mid] < target) lo = mid + 1;
        else hi = mid;
    }

    return (lo < n && vals[lo] == target) ? lo : SIZE_MAX;
}
```

**正确性验证**：

| 测试项 | 方法 |
|--------|------|
| 随机交叉验证 | n=100K, 10000 次随机查询 vs `bsearch()` |
| n=0~3 边界 | n=0,1,2,3，验证不越界（应走标量回退） |
| n=4 对齐边界 | n=4，验证 block=0, block+4=n 不越界 |
| n=5~15 混合 | 部分 SIMD + 标量收尾 |
| INT64_MIN/MAX | target 为极值 |
| 重复元素 | 多个相同 key，返回第一个匹配位置 |

**性能基线**：

| 测试 | 配置 |
|------|------|
| 数据规模 | 10M int64_t（80MB），uniform 分布 |
| 命中率 | 50% hit（随机选择 10K 次查询） |
| 对比基准 | 标量二分（同一文件内实现 int64 标量版本） |
| 测量方式 | `__rdtsc()` 测 cycle，7 次重复取中位数 |
| 输出指标 | AVX2 cy/query, Scalar cy/query, 加速比 |

**编译命令**：
```bash
gcc -O3 -std=c11 -mavx2 -o poc_int64_avx2 src/poc_int64_avx2.c -lm
```

**验收标准**：
- [x] GATE-1 通过：10000 次交叉验证零差异 + 所有边界测试通过 ✅
- [ ] GATE-2 通过：10M 下 AVX2 加速比 ≥ 1.2x ❌ (加速比 0.58x，Path A NO-GO)
- [x] 无 `-fsanitize=address,undefined` 告警 ✅

---

### ACT-15：实现 poc_int64_b1.c [P1]

**目标**：验证 int64 B1（high20 dir + lo44 bucket 4 路扫描）的实用价值。

**交付物**：`src/poc_int64_b1.c`（单文件自包含）

**实现范围**：

1. **目录构建**：
   - `dir[1 << 20 + 1]` = int32_t 数组（4MB + 4B）
   - 遍历排序后的 int64_t vals，按 `target >> 44` 分桶
   - dir[i] 记录桶 i 的起始偏移，dir[1048576] 为哨兵（= n）

2. **桶内扫描**：
   - 4 路 `_mm256_cmpeq_epi64` 并行匹配
   - 命中时二次校验 `vals[pos] == target`（防御假阳性）
   - 标量尾部扫描

3. **Benchmark 矩阵**（6 组）：

| 分布 | 1M | 10M |
|------|-----|------|
| uniform | ✅ | ✅ |
| skewed (80/20) | ✅ | ✅ |
| zipf (α=1.0) | ✅ | ✅ |

每组 7 次重复取中位数，记录 cy/query。

4. **三路对比**：
   - B1 int64（本文件实现）
   - Path A int64（复用 ACT-14 的 `search_int64_avx2()`）
   - 标量二分

**编译命令**：
```bash
gcc -O3 -std=c11 -mavx2 -o poc_int64_b1 src/poc_int64_b1.c -lm
```

**验收标准**：
- [x] GATE-3 检查：10M uniform 下 B1 ≥ 1.5x Path A → 纳入 Phase 1 ✅ (加速比 9.17x)
- [x] 6 组 benchmark 全部完成，数据记录 ✅
- [x] 若 B1 不达标，输出降级建议 → N/A（B1 达标，无需降级）

---

### ACT-16：实现 poc_bloom_bypass.c [P1]

**目标**：验证 meeting_013 方案 C' 的 bloom_bypass 正确性和并发安全。

**交付物**：`src/poc_bloom_bypass.c`（单文件自包含）

**实现范围**：

1. **数据结构**：
```c
typedef struct {
    const int32_t *vals;
    size_t          n;
    void           *bloom;           /* bloom_filter 句柄，可为 NULL */
    _Atomic(int)    bloom_bypass;     /* 0=正常使用bloom, 1=绕过 */
} poc_impl_t;
```

2. **核心函数**：
   - `poc_create()` — 分配数据 + 可选布隆
   - `poc_find()` — 包含 bypass 检查的搜索
   - `poc_set_bloom_bypass()` — 运行时切换
   - `poc_destroy()` — 清理

3. **验证项**（5 项）：

| 编号 | 验证项 | 方法 |
|------|--------|------|
| V-BP-01 | bypass=0 bloom 预筛生效 | 查询不存在的 key，确认被 bloom 拦截返回 NOT_FOUND |
| V-BP-02 | bypass=1 跳过 bloom | 同样不存在的 key，bypass=1 后深入搜索（走 full scan） |
| V-BP-03 | 存在 key 切换一致性 | 存在 key 在 bypass 切换前后返回相同 index |
| V-BP-04 | NULL bloom 安全 no-op | bloom=NULL 句柄上 set_bloom_bypass 不崩溃 |
| V-BP-05 | 并发安全 | 2 线程切换 + 4 线程查询，无崩溃 |

4. **性能对比**（可选，非阻塞）：
   - 1M 100% hit，bypass=0 vs bypass=1，记录 cy/query
   - 预期 bypass 延迟更低

**编译命令**：
```bash
gcc -O3 -std=c11 -mavx2 -o poc_bloom_bypass src/poc_bloom_bypass.c src/xxhash/xxhash.o -lm
```

**验收标准**：
- [x] GATE-4 通过：5 项验证全部通过 ✅
- [x] 并发测试无崩溃（不做 TSan 要求，POC 阶段可接受）✅

---

### ACT-17：更新 README.txt [P0]

**目标**：在项目 README.txt 中新增 POC 编译和运行命令。

**交付物**：`README.txt` 新增 POC 专区

**内容**：
```text
=== Int64 扩展 POC 测试 (2026-06-02, meeting_014) ===

# Path A int64 SIMD 验证 (GATE-1, GATE-2)
gcc -O3 -std=c11 -mavx2 -o poc_int64_avx2 src/poc_int64_avx2.c -lm
./poc_int64_avx2

# Path B1 int64 验证 (GATE-3)
gcc -O3 -std=c11 -mavx2 -o poc_int64_b1 src/poc_int64_b1.c -lm
./poc_int64_b1

# Bloom Bypass 验证 (GATE-4)
gcc -O3 -std=c11 -mavx2 -o poc_bloom_bypass src/poc_bloom_bypass.c src/xxhash/xxhash.o -lm
./poc_bloom_bypass
```

---

### ACT-18：POC 结果汇总 + Go/No-Go 报告 [P1]

**目标**：汇总三份 POC 的执行结果，生成 Go/No-Go 决策报告。

**交付物**：`docs/decisions/poc_int64_report.md`

**内容结构**：
1. GATE-1~4 执行结果表格
2. 性能数据汇总（Path A / B1 / Scalar / Bloom bypass）
3. Go/No-Go 判定（根据 D-106 决策树）
4. 若通过 → 立项建议（下一步：Align 阶段）
5. 若不通过 → 失败根因 + 建议

---

## 📅 执行顺序

```
Day 1-2:
  ACT-14 poc_int64_avx2.c   — Path A SIMD验证 (GATE-1, GATE-2)
  ACT-17 README.txt更新      — 与ACT-14同步

Day 3:
  ACT-15 poc_int64_b1.c      — Path B1 benchmark (GATE-3)
  【可并行】ACT-16 poc_bloom_bypass.c — Bloom旁路验证 (GATE-4)

Day 4:
  ACT-18 POC结果汇总         — Go/No-Go报告
  → 若通过：触发立项工作流 (Align阶段)
  → 若不通过：标记BLOCKED，等待人工决策
```

## 关联信息

- 源会议：meeting_014_poc_design
- 前置会议：meeting_012_int64_feasibility, meeting_013_bloom_toggle
- 决议文档：[03_decisions.md](03_decisions.md)
- 讨论记录：[02_discussion.md](02_discussion.md)
- 承继行动项：meeting_012 ACT-01~ACT-06, meeting_013 ACT-07~ACT-13
- 对现有代码的影响：零修改（仅新增 src/ 下 3 个独立 POC 文件 + README.txt 更新）
