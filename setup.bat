@echo off
rem Double-click to set everything up, or run "setup.bat check|game-files|build|share|install|package".
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
echo.
pause
