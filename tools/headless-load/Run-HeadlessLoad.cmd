@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-HeadlessLoad.ps1" %*
exit /b %ERRORLEVEL%
