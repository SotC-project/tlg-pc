@echo off
rem Creates the Desktop and Start menu shortcuts for the game.
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
pause
