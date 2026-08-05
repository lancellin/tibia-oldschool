@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-TfsAutosendMetrics.ps1" %*
exit /b %ERRORLEVEL%
