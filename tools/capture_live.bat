@echo off
rem Saves the board's log for 3 minutes WITHOUT restarting it, to monitor_live.log
rem (e.g. while a firmware started from MultiDeck is running).
"C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe" "%~dp0capture_live.py" %*
timeout /t 5
