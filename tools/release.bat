@echo off
rem Builds the release image (docs\firmware\multideck.bin + otadata.bin + manifest) with ESP-IDF. Log: tools\release.log
cd /d "%~dp0.."
del /q docs\firmware\otadata.bin 2>nul
git checkout -- .
git pull
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'; & .\release.ps1 2>&1 | Tee-Object tools\release.log"
timeout /t 10
