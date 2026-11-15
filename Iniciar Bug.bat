@echo off
title Servidor de Bug
rem Starts the Bug server on this PC (speech recognition, voice, Claude control, free chat). Leave this window open while you use Bug.
rem First time on a new PC? Run scripts\Instalar.bat first.

set "ROOT=%~dp0"
set "PY=%USERPROFILE%\bug-venv\Scripts\python.exe"

if not exist "%PY%" (
    echo Falta el entorno de Python de Bug. Ejecuta primero scripts\Instalar.bat
    echo.
    pause
    exit /b 1
)

curl -s -m 3 http://127.0.0.1:5000/health >nul 2>&1
if %errorlevel%==0 (
    echo El servidor de Bug ya esta corriendo. No hace falta abrirlo otra vez.
    echo.
    pause
    exit /b
)

rem The free-chat model (Ollama) usually starts with Windows; if it is not running, start it quietly
curl -s -m 2 http://127.0.0.1:11434/api/version >nul 2>&1
if not %errorlevel%==0 (
    where ollama >nul 2>&1 && start "" /min ollama serve
)

cd /d "%ROOT%server"
set PYTHONUNBUFFERED=1
set HF_HUB_DISABLE_SYMLINKS_WARNING=1
echo Arrancando el servidor de Bug... (Whisper tarda unos 10 segundos en cargar)
echo Cuando diga "Speech recognition ready" ya esta listo. No cierres esta ventana.
echo.
"%PY%" bug_server.py

echo.
echo El servidor se detuvo.
pause
