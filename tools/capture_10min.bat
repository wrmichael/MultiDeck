@echo off
rem 10-minute MultiDeck log capture to monitor.log (restarts the board first).
"C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe" "%~dp0capture.py" 600
timeout /t 15
