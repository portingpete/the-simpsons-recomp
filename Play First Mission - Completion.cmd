@echo off
setlocal DisableDelayedExpansion
cd /d "%~dp0"
if errorlevel 1 goto folder_missing
if not exist "%~dp0build\native\SimpsonsLauncher.exe" goto build_missing
start "" "%~dp0build\native\SimpsonsLauncher.exe" --first-mission-completion
if errorlevel 1 goto launch_failed
exit /b 0

:build_missing
echo The Simpsons Game launcher has not been built yet.
echo.
echo Expected file: "%~dp0build\native\SimpsonsLauncher.exe"
echo See "%~dp0docs\launcher.md" for build and setup help.
echo.
pause
exit /b 1

:folder_missing
echo The game folder could not be opened.
echo Place this shortcut in the game's local workspace folder and try again.
echo.
pause
exit /b 1

:launch_failed
echo Windows could not start the first-mission completion launch.
echo See "%~dp0docs\launcher.md" for help checking the build.
echo.
pause
exit /b 1
