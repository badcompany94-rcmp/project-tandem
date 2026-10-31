@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-Tandem.ps1" -Lan
if errorlevel 1 pause
