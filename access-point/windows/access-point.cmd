@echo off
rem Runs access-point.ps1 without changing the PowerShell execution policy.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0access-point.ps1" %*
