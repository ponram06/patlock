@echo off
setlocal
rem VM ONLY. Pulls the latest commits, then builds, tests and deploys Patlock.

rem Relaunch as Administrator if needed (deploying to System32 requires it).
net session >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Requesting Administrator rights...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"
echo Pulling latest commits from git...
git pull --ff-only origin main
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] git pull failed. Nothing was built or installed.
    pause
    exit /b 1
)
echo.
echo Building, testing and deploying Patlock...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install\update.ps1" -AcknowledgeVm
set RESULT=%ERRORLEVEL%
if %RESULT% NEQ 0 (
    echo.
    echo [ERROR] Update failed with error code %RESULT%. The previous provider is still in place.
    pause
    exit /b %RESULT%
)
pause
