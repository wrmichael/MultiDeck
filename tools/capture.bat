@echo off
rem Saves the MultiDeck boot log to monitor.log in this folder (restarts the board first).
"C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe" "%~dp0capture.py" %*
echo.
echo Saved to %~dp0monitor.log
timeout /t 15
