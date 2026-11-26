@echo off
rem Update MultiDeck from GitHub, build and flash it (close any idf.py monitor first).
rem Writes bootloader, partition table, MultiDeck (factory) and a blank otadata: the board starts MultiDeck.
rem RigDeck's settings, log and Wi-Fi chip image, and the firmware in the app slots, are kept.
cd /d "%~dp0.."
rem release.bat rewrites docs\ locally; the published copies come from GitHub, so drop the local ones first
del /q docs\firmware\otadata.bin 2>nul
git checkout -- .
git pull
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'; Set-Location '%~dp0..'; idf.py flash 2>&1 | Tee-Object -FilePath tools\flash.log"
echo.
echo Finished. See tools\flash.log
timeout /t 15
