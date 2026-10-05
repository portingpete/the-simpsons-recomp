param([switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'
$root = 'K:\SimpsonsNativeCopy'
$state = Join-Path $root 'build\native-continuation-loop'
$promptPath = Join-Path $root 'tools\native_continuation_prompt.txt'
$model = 'opencode/muse-spark-1.3-contributor-free'
foreach ($path in @($promptPath)) {
    if (!(Test-Path $path)) { throw "Missing prerequisite: $path" }
}
try { $null = Get-Command opencode -ErrorAction Stop } catch { throw 'Missing prerequisite: opencode CLI' }
if ($ValidateOnly) { Write-Output 'In-game loop prerequisites valid (opencode CLI + continuation prompt)'; exit 0 }
New-Item -ItemType Directory -Force -Path $state | Out-Null
$lock = $null
try {
    $lock = [IO.File]::Open((Join-Path $state 'worker.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    Set-Content (Join-Path $state 'pid.txt') $PID
    $iteration = 0
    while (!(Test-Path (Join-Path $state 'STOP'))) {
        $iteration++
        $id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $iteration
        $result = Join-Path $state ($id + '-result.json')
        $log = Join-Path $state ($id + '.log')
        $prompt = [IO.File]::ReadAllText($promptPath) + "`nWrite this iteration's result JSON to: $result"
        Set-Content (Join-Path $state 'status.txt') "RUNNING iteration=$id result=$result log=$log"
        $ErrorActionPreference = 'Continue'
        $code = 1
        try {
            $job = Start-Job -ScriptBlock {
                param($rootDir, $modelId, $message)
                Set-Location $rootDir
                & opencode run --model $modelId --dangerously-skip-permissions --dir $rootDir $message 2>&1
                $LASTEXITCODE
            } -ArgumentList $root, $model, $prompt
            $finished = Wait-Job $job -Timeout 3600
            if (!$finished) { Stop-Job $job; Remove-Job $job -Force; throw 'Worker timed out after 3600s' }
            $output = Receive-Job $job
            Remove-Job $job
            $code = 1
            $lines = @()
            if ($output -is [array]) {
                if ($output.Count -gt 0 -and $output[-1] -is [int]) { $code = $output[-1]; $lines = $output[0..($output.Count - 2)] }
                else { $lines = $output; $code = 0 }
            } elseif ($output -is [int]) { $code = $output }
            else { $lines = @($output); $code = 0 }
            $lines | Out-File -FilePath $log -Encoding utf8
        } catch {
            ("Worker exception: " + $_.Exception.Message) | Out-File -FilePath $log -Encoding utf8
            $code = 1
        } finally { $ErrorActionPreference = 'Stop' }
        if ($code -ne 0) { throw "Worker exited $code. Inspect $log; automatic retries disabled." }
        if (!(Test-Path $result)) { throw "Worker omitted result checkpoint: $result" }
        $report = Get-Content $result -Raw | ConvertFrom-Json
        if ($report.status -notin @('continue','complete','blocked')) { throw 'Invalid result status' }
        if (!$report.summary -or !$report.evidence) { throw 'Result lacks summary or evidence' }
        if ($report.status -ne 'continue') {
            Set-Content (Join-Path $state 'status.txt') "$($report.status.ToUpper()) $($report.summary) result=$result"
            break
        }
        if (!$report.next -or !$report.changed_files -or !$report.tests_passed) {
            throw 'No verified implementation progress; stopping rather than repeating diagnostics'
        }
        Set-Content (Join-Path $state 'status.txt') "WAITING 30 seconds; completed=$id next=$($report.next)"
        Start-Sleep -Seconds 30
    }
    if (Test-Path (Join-Path $state 'STOP')) { Set-Content (Join-Path $state 'status.txt') 'STOPPED by request' }
} catch {
    if ($lock) { Set-Content (Join-Path $state 'status.txt') "BLOCKED $($_.Exception.Message)" }
    throw
} finally {
    if ($lock) {
        Remove-Item (Join-Path $state 'pid.txt') -ErrorAction SilentlyContinue
        $lock.Dispose()
    }
}
