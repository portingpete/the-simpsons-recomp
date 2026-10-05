param([switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'
$root = 'K:\SimpsonsNativeCopy'
$state = Join-Path $root 'build\native-continuation-loop'
$node = 'C:\nvm4w\nodejs\node.exe'
$cli = 'C:\nvm4w\nodejs\node_modules\cline\bin\cline'
$promptPath = Join-Path $root 'tools\native_continuation_prompt.txt'
foreach ($path in @($node,$cli,$promptPath)) {
    if (!(Test-Path $path)) { throw "Missing prerequisite: $path" }
}
if ($ValidateOnly) { Write-Output 'Loop prerequisites valid'; exit 0 }
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
        # Direct node invocation avoids the npm PowerShell shim's exit statement.
        # Windows PowerShell wraps native stderr in ErrorRecord objects even
        # for progress messages. Judge failure by exit code, not stderr text.
        $ErrorActionPreference = 'Continue'
        try {
            & $node $cli --timeout 3600 --retries 3 --cwd $root $prompt > $log 2>&1
            $code = $LASTEXITCODE
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
