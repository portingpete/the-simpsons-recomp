# Windows PowerShell 5.1. Regeneration must precede the native build.
[CmdletBinding()]
param([ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$RunName = 'reach-game-213')
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE; no later step was executed."
    }
}
Push-Location $Root
try {
    foreach ($Name in @('python', 'cmake')) {
        if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) { throw "Required command is unavailable: $Name" }
    }
    $Required = @(
        'tools/recompile.py', 'runtime/rigid_packet_owner.h',
        'tools/gameplay_evidence.py', 'tools/auto_start_native.py',
        'analysis/simpsons.pe', 'analysis/simpsons.unencrypted.xex',
        'config/startup_replay.json', 'config/native_sources.json',
        'build/native/CMakeCache.txt',
        'build/generator-ninja/XenonRecomp/XenonRecomp.exe',
        'build/generator-ninja/XenonAnalyse/XenonAnalyse.exe'
    )
    foreach ($Name in $Required) {
        if (-not (Test-Path -LiteralPath $Name -PathType Leaf)) {
            throw "Required full-workspace file is missing: $Name. This handoff archive is not a standalone game build."
        }
    }
    $RunDirectory = Join-Path 'build/automatic-startup' $RunName
    if (Test-Path -LiteralPath $RunDirectory) { throw "Run directory already exists; choose a new -RunName: $RunDirectory" }
    Invoke-Checked 'python' @('-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_gameplay_evidence.py', '-v')
    Invoke-Checked 'python' @('-B', 'tools/recompile.py',
        '--generator', 'build/generator-ninja/XenonRecomp/XenonRecomp.exe',
        '--analyser', 'build/generator-ninja/XenonAnalyse/XenonAnalyse.exe')
    Invoke-Checked 'cmake' @('--build', 'build/native', '--parallel', '8')
    Invoke-Checked 'python' @('-B', 'tools/auto_start_native.py', '--run-directory', $RunDirectory, '--until', 'game')
    $Result = Get-Content -LiteralPath (Join-Path $RunDirectory 'result.json') -Raw | ConvertFrom-Json
    if (-not $Result.success -or -not $Result.gameplay_verified -or -not $Result.gameplay_evidence) {
        throw 'The launcher returned without a verified gameplay-frame receipt.'
    }
    Write-Output ('Verified gameplay evidence: ' + (Join-Path $RunDirectory 'result.json'))
} finally {
    Pop-Location
}
