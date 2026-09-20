@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0START-DRONE-SYSTEM.ps1" %*
endlocal
