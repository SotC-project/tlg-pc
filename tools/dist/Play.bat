@echo off
rem The Last Guardian - starts the game. Saves and settings live in .\user
if not exist "%~dp0The Last Guardian.exe" (
  echo Program not found: %~dp0The Last Guardian.exe
  pause
  exit /b 1
)
cd /d "%~dp0"
start "" "%~dp0The Last Guardian.exe" %*
