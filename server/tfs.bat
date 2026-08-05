@echo off
cd /d "%~dp0"

tasklist /FI "IMAGENAME eq player_io_service.exe" 2>NUL | find /I "player_io_service.exe" >NUL
if errorlevel 1 (
	start "TFS Player I/O Service" /min player_io_service.exe player-io-service.conf
	timeout /t 1 /nobreak >NUL
)

tfs.exe
