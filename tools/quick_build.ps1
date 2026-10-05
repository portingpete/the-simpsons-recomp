# Incremental SimpsonsNative build (toolchain env + cmake --build). Usage: tools/quick_build.ps1 [-Recompile] [-Configure -DOPTION=VALUE,...]
param([switch]$Recompile, [string]$Target = 'SimpsonsNative', [string[]]$Configure = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -property installationPath
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;$env:PATH"
$lines = & cmd.exe /d /s /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
foreach ($l in $lines) { if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }
$sdkBin = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/bin/*/x64/rc.exe' | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty DirectoryName
$env:PATH = "$sdkBin;$env:PATH"
Set-Location $root
if ($Recompile) {
    python -B tools/recompile.py --generator build/generator-ninja/XenonRecomp/XenonRecomp.exe --analyser build/generator-ninja/XenonAnalyse/XenonAnalyse.exe
    if ($LASTEXITCODE -ne 0) { throw 'recompile failed' }
}
if ($Configure.Count) { cmake -S . -B build/native @Configure; if ($LASTEXITCODE -ne 0) { throw 'configure failed' } }
cmake --build build/native --target $Target
exit $LASTEXITCODE
