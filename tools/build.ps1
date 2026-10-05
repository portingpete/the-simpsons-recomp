param([switch]$Diagnostic, [switch]$GeneratorOnly, [switch]$SkipGenerate, [int]$Jobs = 6)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -property installationPath
if (-not $vsPath) { throw 'A Visual Studio C++ installation is required.' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
# Capture the compiler environment only. No file operations cross shells.
$environmentLines = & cmd.exe /d /s /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the x64 toolchain.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}
$clang = 'C:/Program Files/LLVM/bin/clang-cl.exe'
$sdkBin = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/bin/*/x64/rc.exe' |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty DirectoryName
if (-not $sdkBin) { throw 'Windows SDK resource compiler is missing.' }
$env:PATH = "$sdkBin;$env:PATH"
$generatorBuild = Join-Path $projectRoot 'build/generator-ninja'
Invoke-Checked cmake @('-S', "$projectRoot/third_party/XenonRecomp", '-B', $generatorBuild,
    '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_C_COMPILER=$clang", "-DCMAKE_CXX_COMPILER=$clang", "-DCMAKE_RC_COMPILER=$(($sdkBin -replace '\\','/'))/rc.exe")
Invoke-Checked python @("$PSScriptRoot/generator_identity.py", 'capture')
Invoke-Checked cmake @('--build', $generatorBuild, '--target', 'XenonRecomp', 'XenonAnalyse', 'SimpsonsDisasm', '--parallel', "$Jobs")
Invoke-Checked python @("$PSScriptRoot/generator_identity.py", 'seal')
if ($GeneratorOnly) { return }
if (-not $SkipGenerate) {
    Invoke-Checked python @("$PSScriptRoot/prepare_image.py")
    $generateArgs = @("$PSScriptRoot/recompile.py", '--generator', "$generatorBuild/XenonRecomp/XenonRecomp.exe",
        '--analyser', "$generatorBuild/XenonAnalyse/XenonAnalyse.exe")
    if ($Diagnostic) { $generateArgs += '--diagnostic' }
    Invoke-Checked python $generateArgs
}
$allow = if ($Diagnostic) { 'ON' } else { 'OFF' }
Invoke-Checked cmake @('-S', $projectRoot, '-B', "$projectRoot/build/native", '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_CXX_COMPILER=$clang", "-DSIMPSONS_DIAGNOSTIC=$allow")
Invoke-Checked cmake @('--build', "$projectRoot/build/native", '--parallel', "$Jobs")
Invoke-Checked ctest @('--test-dir', "$projectRoot/build/native", '--output-on-failure')
