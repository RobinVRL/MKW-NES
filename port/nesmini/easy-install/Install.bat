@echo off
rem Double-click to install the Mario Kart Wii menu entry on the NES/SNES Classic Mini.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
echo.
pause
