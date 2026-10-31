@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-Tandem.ps1" -NetworkProfile Responsive
if errorlevel 1 pause
