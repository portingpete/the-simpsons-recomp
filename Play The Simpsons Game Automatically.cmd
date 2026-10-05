@echo off
setlocal DisableDelayedExpansion
cd /d "%~dp0"
if errorlevel 1 goto failed
if not exist "%~dp0build\native\SimpsonsNative.exe" goto failed
if not exist "%~dp0config\startup_replay.json" goto failed
where pythonw.exe >nul 2>nul
if errorlevel 1 goto failed
start "" /D "%~dp0" pythonw.exe -B "%~dp0tools\auto_start_native.py"
if errorlevel 1 goto failed
exit /b 0

:failed
echo Automatic startup could not be opened.
echo Check that the native game build and Python are installed.
echo See docs\automatic-startup.md for details.
pause
exit /b 1
