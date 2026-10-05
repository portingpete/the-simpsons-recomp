@echo off
setlocal DisableDelayedExpansion
cd /d "%~dp0"
if errorlevel 1 goto failed
python.exe -B "%~dp0tools\render_test_native.py" %*
if errorlevel 1 goto failed
exit /b 0

:failed
echo The first-mission rendering test could not be launched.
echo Check the native build and Python installation.
pause
exit /b 1
