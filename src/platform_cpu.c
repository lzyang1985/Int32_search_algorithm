#include "platform_cpu.h"

/*
 * platform_cpu_has_avx2() — CPU 硬件 AVX2 能力检测
 *
 * 注意：本函数仅检测 CPU 是否支持 AVX2 指令集，不反映编译器是否能生成
 * 高质量 AVX2 代码。在 MinGW GCC 等环境下，即使 CPU 硬件支持 AVX2，
 * 编译器生成的 AVX2 代码也可能因代码生成质量导致性能退化。
 * 实际性能应以基准测试（benchmark）结果为准。
 *
 * 实现：首次调用通过 __builtin_cpu_supports("avx2") 查询 CPUID，
 * 后续调用返回缓存结果（无锁，idempotent）。
 */

int platform_cpu_has_avx2(void)
{
    static int cached = -1;
    if (cached == -1) {
        cached = __builtin_cpu_supports("avx2") ? 1 : 0;
    }
    return cached;
}
