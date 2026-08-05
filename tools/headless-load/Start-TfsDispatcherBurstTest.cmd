@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-TfsDispatcherBurstTest.ps1" %*
exit /b %ERRORLEVEL%
