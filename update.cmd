@echo off
setlocal
echo Pulling latest commits from git...
git pull origin main
echo.
echo Running Patlock VM updater...
powershell -ExecutionPolicy Bypass -File "%~dp0install\update.ps1" -AcknowledgeVm
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Update failed with error code %ERRORLEVEL%.
    pause
    exit /b %ERRORLEVEL%
)
