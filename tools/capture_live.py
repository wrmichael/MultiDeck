# Like capture.py, but does not restart the board: saves to tools\monitor_live.log.
# Usage: capture_live.bat [COMx] [seconds]
import os, runpy
os.environ["CAPTURE_LIVE"] = "1"
runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), "capture.py"), run_name="__main__")
