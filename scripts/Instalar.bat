@echo off
rem Runs the installer without changing the PowerShell execution policy for the rest of the PC.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Instalar.ps1" %*
echo.
pause
