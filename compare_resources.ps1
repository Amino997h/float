$floatProc = Get-Process Float -ErrorAction SilentlyContinue
if (-not $floatProc) {
    Write-Output "Float is not currently running."
    exit
}

$floatPid = $floatProc.Id
$mainRam = [math]::Round($floatProc.WorkingSet64 / 1MB, 2)

# Query child WebView2 processes spawned by Float
$children = Get-CimInstance Win32_Process | Where-Object { $_.ParentProcessId -eq $floatPid }
$totalBytes = $floatProc.WorkingSet64
$list = @()
$list += [PSCustomObject]@{ Process = "Float (C++ Shell)"; PID = $floatPid; RAM_MB = $mainRam }

foreach ($c in $children) {
    $p = Get-Process -Id $c.ProcessId -ErrorAction SilentlyContinue
    if ($p) {
        $totalBytes += $p.WorkingSet64
        $r = [math]::Round($p.WorkingSet64 / 1MB, 2)
        $list += [PSCustomObject]@{ Process = "WebView2 Worker"; PID = $p.Id; RAM_MB = $r }
    }
}

$floatTotalMB = [math]::Round($totalBytes / 1MB, 2)

Write-Host "===============================================================" -ForegroundColor Cyan
Write-Host "                    BROWSERS RESOURCE BENCHMARK                " -ForegroundColor Cyan
Write-Host "===============================================================" -ForegroundColor Cyan

$results = @()
$results += [PSCustomObject]@{
    Browser = "Float (Your Browser)"
    Processes = $list.Count
    "Total RAM (MB)" = $floatTotalMB
    Status = "Ultra-Lightweight"
}

# 1. Google Chrome
$chrome = Get-Process chrome -ErrorAction SilentlyContinue
if ($chrome) {
    $chromeTotal = [math]::Round(($chrome | Measure-Object -Property WorkingSet64 -Sum).Sum / 1MB, 2)
    $results += [PSCustomObject]@{
        Browser = "Google Chrome"
        Processes = $chrome.Count
        "Total RAM (MB)" = $chromeTotal
        Status = "Running"
    }
} else {
    $results += [PSCustomObject]@{
        Browser = "Google Chrome"
        Processes = 0
        "Total RAM (MB)" = 0
        Status = "Not Running"
    }
}

# 2. Mozilla Firefox
$firefox = Get-Process firefox -ErrorAction SilentlyContinue
if ($firefox) {
    $firefoxTotal = [math]::Round(($firefox | Measure-Object -Property WorkingSet64 -Sum).Sum / 1MB, 2)
    $results += [PSCustomObject]@{
        Browser = "Mozilla Firefox"
        Processes = $firefox.Count
        "Total RAM (MB)" = $firefoxTotal
        Status = "Running"
    }
} else {
    $results += [PSCustomObject]@{
        Browser = "Mozilla Firefox"
        Processes = 0
        "Total RAM (MB)" = 0
        Status = "Not Running"
    }
}

# 3. Microsoft Edge
$edge = Get-Process msedge -ErrorAction SilentlyContinue
if ($edge) {
    $edgeTotal = [math]::Round(($edge | Measure-Object -Property WorkingSet64 -Sum).Sum / 1MB, 2)
    $results += [PSCustomObject]@{
        Browser = "Microsoft Edge"
        Processes = $edge.Count
        "Total RAM (MB)" = $edgeTotal
        Status = "Running"
    }
} else {
    $results += [PSCustomObject]@{
        Browser = "Microsoft Edge"
        Processes = 0
        "Total RAM (MB)" = 0
        Status = "Not Running"
    }
}

$results | Format-Table -AutoSize

Write-Host "---------------------------------------------------------------" -ForegroundColor Gray
Write-Host "Float Breakdown:" -ForegroundColor Green
$list | Format-Table -AutoSize
Write-Host "===============================================================" -ForegroundColor Cyan
