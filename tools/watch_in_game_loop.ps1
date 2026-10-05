param([switch]$Once)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$state = Join-Path $root 'build\native-continuation-loop'

function Get-Snapshot {
    $lines = @()
    $lines += 'In-game loop watcher'
    $lines += ('Workspace: ' + $root)
    $lines += ''
    if (!(Test-Path $state)) { $lines += 'Loop state folder is missing: build\native-continuation-loop'; return $lines }
    $statusPath = Join-Path $state 'status.txt'
    if (Test-Path $statusPath) { $lines += ('status.txt: ' + ((Get-Content $statusPath -Raw).Trim())) }
    else { $lines += 'status.txt: missing' }
    $stopPath = Join-Path $state 'STOP'
    $lines += ('STOP file: ' + ($(if (Test-Path $stopPath) { 'present (loop will stop)' } else { 'absent' })))
    foreach ($name in @('pid.txt', 'launcher.pid')) {
        $path = Join-Path $state $name
        if (!(Test-Path $path)) { $lines += ($name + ': missing'); continue }
        $id = ((Get-Content $path -Raw).Trim())
        $alive = $false
        try { $alive = [bool](Get-Process -Id ([int]$id) -ErrorAction Stop) } catch { $alive = $false }
        $lines += ($name + ': ' + $id + $(if ($alive) { ' (running)' } else { ' (not running)' }))
    }
    $lines += ''
    $results = Get-ChildItem -Path $state -Filter '*-result.json' -ErrorAction SilentlyContinue | Sort-Object Name -Descending
    if ($results -and $results.Count -gt 0) {
        $latest = $results[0]
        $lines += ('Latest result: ' + $latest.Name)
        try {
            $report = Get-Content $latest.FullName -Raw | ConvertFrom-Json
            $lines += ('  status: ' + $report.status)
            if ($report.summary) { $lines += ('  summary: ' + ([string]$report.summary)) }
            if ($report.next) { $lines += ('  next: ' + ([string]$report.next)) }
            $lines += ('  tests_passed: ' + $report.tests_passed)
        } catch { $lines += ('  (could not parse: ' + $_.Exception.Message + ')') }
    } else {
        $lines += 'Latest result: none yet (first iteration still running or no iteration completed)'
    }
    $logs = Get-ChildItem -Path $state -Filter '*.log' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending
    if ($logs -and $logs.Count -gt 0) {
        $latest = $logs[0]
        $lines += ('Latest log: ' + $latest.Name)
        try {
            $tail = Get-Content $latest.FullName -Tail 12 -ErrorAction Stop
            foreach ($t in $tail) { $lines += ('  ' + $t) }
        } catch { $lines += '  (log not readable yet)' }
    } else {
        $lines += 'Latest log: none yet'
    }
    $lines += ''
    $recent = Get-ChildItem -Path $state -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 6
    if ($recent) {
        $lines += 'Recent state files (newest first):'
        foreach ($f in $recent) { $lines += ('  ' + $f.Name) }
    }
    return $lines
}

if ($Once) { Get-Snapshot | ForEach-Object { Write-Output $_ }; exit 0 }

while ($true) {
    Clear-Host
    Get-Snapshot | ForEach-Object { Write-Output $_ }
    Write-Output ''
    Write-Output '[R]efresh  [O]pen state folder  [S]top loop (creates STOP)  [Q]uit watcher (loop keeps running)'
    $choice = Read-Host 'Choice'
    if (!$choice) { continue }
    switch ($choice.Trim().ToUpperInvariant()) {
        'R' { continue }
        'O' { Start-Process explorer.exe -ArgumentList $state; continue }
        'S' {
            $confirm = Read-Host 'Create STOP file and let the loop exit after this iteration? (y/N)'
            if ($confirm.Trim().ToUpperInvariant() -eq 'Y') {
                New-Item -ItemType File -Force -Path (Join-Path $state 'STOP') | Out-Null
                Write-Output 'STOP created. The loop will exit after its current iteration.'
                Start-Sleep -Seconds 2
            }
            continue
        }
        'Q' { Write-Output 'Watcher closed. The background loop keeps running.'; exit 0 }
        default { continue }
    }
}
