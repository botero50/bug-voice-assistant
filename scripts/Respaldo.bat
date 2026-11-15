@echo off
rem Saves or restores the private files of Bug (tokens, e-mail, calendar). See Respaldo.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Respaldo.ps1" %*
echo.
pause
