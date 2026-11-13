@echo off
setlocal
title Patlock - Windows 11 VMware test
if not exist "%~dp0install\run-vm-test.ps1" (
  echo Missing install\run-vm-test.ps1. Extract or clone the entire repository.
  pause
  exit /b 1
)
set "PATLOCK_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PATLOCK_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%PATLOCK_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0install\run-vm-test.ps1"
set "PATLOCK_RESULT=%ERRORLEVEL%"
echo.
if not "%PATLOCK_RESULT%"=="0" echo Test workflow stopped. Read the error above before retrying.
pause
exit /b %PATLOCK_RESULT%
