# bench_100.ps1 — 每个demo运行100次，统计 Average per query (us)
param(
    [int]$Runs = 100
)

$demo_dir = $PSScriptRoot

# 并行数组：names / v1_exes / v3_exes
$names    = @("Int32 Search (default, ~50% hit)", "Int64 Search (~50% hit)", "Int32 Search (Bloom OFF, ~50% hit)", "Int32 Search (Bloom OFF, 100% hit)")
$v1_exes  = @("demo_search.exe", "demo_int64_search.exe", "demo_int32_no_bloom.exe", "demo_int32_no_bloom_fullhit.exe")
$v3_exes  = @("demo_search_v3.exe", "demo_int64_search_v3.exe", "demo_int32_no_bloom_v3.exe", "demo_int32_no_bloom_fullhit_v3.exe")

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

$total = $names.Count * 2 * $Runs
$done = 0
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
        Write-Host "  [V1] $exe - running $Runs times..." -ForegroundColor Yellow
        for ($i = 1; $i -le $Runs; $i++) {
            $val = Run-Bench -ExePath $exe_path
            if ($null -ne $val) { $vals += $val }
            $done++
            $pct = [int](($done / $total) * 100)
            Write-Progress -Activity "Benchmarking..." -Status "$done/$total ($pct%) - $exe run $i" -PercentComplete $pct
        }
        if ($vals.Count -gt 0) { $results["$name|V1"] = $vals }
    } else {
        Write-Warning "SKIP: $exe not found"
        $done += $Runs
    }

    # V3
    $exe = $v3_exes[$idx]
    $exe_path = Join-Path $demo_dir $exe
    $vals = @()
    if (Test-Path $exe_path) {
        Write-Host "  [V3] $exe - running $Runs times..." -ForegroundColor Yellow
        for ($i = 1; $i -le $Runs; $i++) {
            $val = Run-Bench -ExePath $exe_path
            if ($null -ne $val) { $vals += $val }
            $done++
            $pct = [int](($done / $total) * 100)
            Write-Progress -Activity "Benchmarking..." -Status "$done/$total ($pct%) - $exe run $i" -PercentComplete $pct
        }
        if ($vals.Count -gt 0) { $results["$name|V3"] = $vals }
    } else {
        Write-Warning "SKIP: $exe not found"
        $done += $Runs
    }
}

Write-Progress -Activity "Benchmarking..." -Completed

# ===== 输出汇总 =====
Write-Host ""
Write-Host "########################################################################" -ForegroundColor Green
Write-Host "#                      BENCHMARK RESULTS (100 runs)                     #" -ForegroundColor Green
Write-Host "########################################################################" -ForegroundColor Green
Write-Host ""

for ($idx = 0; $idx -lt $names.Count; $idx++) {
    $name = $names[$idx]
    $key_v1 = "$name|V1"
    $key_v3 = "$name|V3"

    Write-Host "------------------------------------------------------------" -ForegroundColor Cyan
    Write-Host "  $name" -ForegroundColor Cyan
    Write-Host "------------------------------------------------------------" -ForegroundColor Cyan

    Write-Host ("{0,-18} {1,>12} {2,>12} {3,>12}" -f "Version", "Min (us)", "Max (us)", "Avg (us)")
    Write-Host ("{0,-18} {1,>12} {2,>12} {3,>12}" -f "--------------------", "------------", "------------", "------------")

    foreach ($key in @($key_v1, $key_v3)) {
        if ($results.ContainsKey($key)) {
            $arr = $results[$key]
            $min = ($arr | Measure-Object -Minimum).Minimum
            $max = ($arr | Measure-Object -Maximum).Maximum
            $avg = ($arr | Measure-Object -Average).Average
            $label = if ($key -eq $key_v1) { "V1 ($($v1_exes[$idx]))" } else { "V3 ($($v3_exes[$idx]))" }
            Write-Host ("{0,-18} {1,12:F2} {2,12:F2} {3,12:F2}" -f $label, $min, $max, $avg)
        }
    }

    # 对比
    if ($results.ContainsKey($key_v1) -and $results.ContainsKey($key_v3)) {
        $arr_v1 = $results[$key_v1]
        $arr_v3 = $results[$key_v3]
        $avg_v1 = ($arr_v1 | Measure-Object -Average).Average
        $avg_v3 = ($arr_v3 | Measure-Object -Average).Average
        if ($avg_v1 -gt 0) {
            $ratio = ($avg_v1 - $avg_v3) / $avg_v1 * 100.0
            if ($ratio -gt 0) {
                $sign = "faster"
            } else {
                $sign = "slower"
            }
            Write-Host ""
            Write-Host ("  V3 vs V1: avg {0:F2}% {1} ({2:F2} us vs {3:F2} us)" -f [Math]::Abs($ratio), $sign, $avg_v3, $avg_v1) -ForegroundColor Magenta
        }
    }
    Write-Host ""
}

Write-Host "Done." -ForegroundColor Green
