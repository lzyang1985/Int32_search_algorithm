# bench_100.ps1 — 每个demo运行100次，统计分位数 (ACT-63 增强版)
# 增强: 分位数 P50/P90/P95/P99 + StdDev + 前5轮丢弃(warmup) + 关闭 Write-Progress
param(
    [int]$Runs = 100,
    [int]$WarmupRuns = 5
)

$demo_dir = $PSScriptRoot

$names   = @("Int32 Search (default, ~50% hit)", "Int64 Search (~50% hit)", "Int32 Search (Bloom OFF, ~50% hit)", "Int32 Search (Bloom OFF, 100% hit)")
$v1_exes = @("demo_search.exe", "demo_int64_search.exe", "demo_int32_no_bloom.exe", "demo_int32_no_bloom_fullhit.exe")
$v4_exes = @("demo_search_v4.exe", "demo_int64_search_v4.exe", "demo_int32_no_bloom_v4.exe", "demo_int32_no_bloom_fullhit_v4.exe")

function Run-Bench {
    param([string]$ExePath)
    $output = & $ExePath 2>&1
    foreach ($line in $output) {
        if ($line -match 'Average per query:\s+([\d.]+)\s*us') {
            return [double]$Matches[1]
        }
    }
    Write-Warning "WARNING: Could not parse output from $ExePath"
    return $null
}

function Get-Percentile {
    param([double[]]$SortedValues, [double]$Percentile)
    if ($SortedValues.Count -eq 0) { return 0 }
    $idx = [Math]::Ceiling($SortedValues.Count * $Percentile / 100.0) - 1
    if ($idx -lt 0) { $idx = 0 }
    if ($idx -ge $SortedValues.Count) { $idx = $SortedValues.Count - 1 }
    return $SortedValues[$idx]
}

function Get-StdDev {
    param([double[]]$Values)
    $avg = ($Values | Measure-Object -Average).Average
    $sumSq = 0.0
    foreach ($v in $Values) { $sumSq += ($v - $avg) * ($v - $avg) }
    if ($Values.Count -le 1) { return 0 }
    return [Math]::Sqrt($sumSq / ($Values.Count - 1))
}

# 实际运行次数 = 要求的有效次数 + 预热丢弃次数
$totalRuns = $Runs + $WarmupRuns
$results = @{}

for ($idx = 0; $idx -lt $names.Count; $idx++) {
    $name = $names[$idx]

    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  $name" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    # V1
    $exe = $v1_exes[$idx]
    $exe_path = Join-Path $demo_dir $exe
    $vals = @()
    if (Test-Path $exe_path) {
        Write-Host "  [V1] $exe - warmup $WarmupRuns + $Runs runs..." -ForegroundColor Yellow
        for ($i = 1; $i -le $totalRuns; $i++) {
            $val = Run-Bench -ExePath $exe_path
            if ($null -ne $val) {
                if ($i -gt $WarmupRuns) { $vals += $val }
            }
            if ($i % 10 -eq 0) { Write-Host "    [$i/$totalRuns]" }
        }
        if ($vals.Count -gt 0) { $results["$name|V1"] = $vals }
        Write-Host "    done: $($vals.Count) valid runs" -ForegroundColor Green
    } else {
        Write-Warning "  SKIP: $exe not found"
    }

    # V4
    $exe = $v4_exes[$idx]
    $exe_path = Join-Path $demo_dir $exe
    $vals = @()
    if (Test-Path $exe_path) {
        Write-Host "  [V4] $exe - warmup $WarmupRuns + $Runs runs..." -ForegroundColor Yellow
        for ($i = 1; $i -le $totalRuns; $i++) {
            $val = Run-Bench -ExePath $exe_path
            if ($null -ne $val) {
                if ($i -gt $WarmupRuns) { $vals += $val }
            }
            if ($i % 10 -eq 0) { Write-Host "    [$i/$totalRuns]" }
        }
        if ($vals.Count -gt 0) { $results["$name|V4"] = $vals }
        Write-Host "    done: $($vals.Count) valid runs" -ForegroundColor Green
    } else {
        Write-Warning "  SKIP: $exe not found"
    }
}

# ===== 输出汇总 (分位数增强版) =====
Write-Host ""
Write-Host "########################################################################" -ForegroundColor Green
Write-Host "#           BENCHMARK RESULTS (${Runs} runs, ${WarmupRuns} warmup discarded)         #" -ForegroundColor Green
Write-Host "########################################################################" -ForegroundColor Green
Write-Host ""

for ($idx = 0; $idx -lt $names.Count; $idx++) {
    $name = $names[$idx]
    $key_v1 = "$name|V1"
    $key_v4 = "$name|V4"

    Write-Host "------------------------------------------------------------" -ForegroundColor Cyan
    Write-Host "  $name" -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------" -ForegroundColor Cyan

    $header = "{0,-35} {1,10} {2,10} {3,10} {4,10} {5,10} {6,10} {7,10}" -f `
        "Version", "Min", "P50", "Avg", "P90", "P95", "P99", "Max"
    Write-Host $header
    Write-Host ("{0,-35} {1,10} {2,10} {3,10} {4,10} {5,10} {6,10} {7,10}" -f `
        "--------------------------------", "----------", "----------", "----------", "----------", "----------", "----------", "----------")

    foreach ($key in @($key_v1, $key_v4)) {
        if (-not $results.ContainsKey($key)) { continue }

        $arr = $results[$key] | Sort-Object
        $min   = $arr[0]
        $max   = $arr[-1]
        $avg   = ($arr | Measure-Object -Average).Average
        $p50   = Get-Percentile -SortedValues $arr -Percentile 50
        $p90   = Get-Percentile -SortedValues $arr -Percentile 90
        $p95   = Get-Percentile -SortedValues $arr -Percentile 95
        $p99   = Get-Percentile -SortedValues $arr -Percentile 99
        $std   = Get-StdDev -Values $arr

        $label = if ($key -eq $key_v1) { "V1" } else { "V4" }
        Write-Host ("{0,-35} {1,10:F2} {2,10:F2} {3,10:F2} {4,10:F2} {5,10:F2} {6,10:F2} {7,10:F2}" -f `
            "$label ($($arr.Count) runs)", $min, $p50, $avg, $p90, $p95, $p99, $max)
        Write-Host ("  StdDev={0:F3} us" -f $std)
    }

    # V1 vs V4 对比
    if ($results.ContainsKey($key_v1) -and $results.ContainsKey($key_v4)) {
        $arr_v1 = $results[$key_v1]
        $arr_v4 = $results[$key_v4]
        $avg_v1 = ($arr_v1 | Measure-Object -Average).Average
        $avg_v4 = ($arr_v4 | Measure-Object -Average).Average
        if ($avg_v1 -gt 0) {
            $ratio = ($avg_v1 - $avg_v4) / $avg_v1 * 100.0
            $sign = if ($ratio -gt 0) { "faster" } else { "slower" }
            Write-Host ""
            Write-Host ("  V4 vs V1: avg {0:F2}% {1} ({2:F2} us vs {3:F2} us)" -f `
                [Math]::Abs($ratio), $sign, $avg_v4, $avg_v1) -ForegroundColor Magenta
        }
    }
    Write-Host ""
}

Write-Host "Done." -ForegroundColor Green
