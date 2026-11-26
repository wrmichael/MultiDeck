# Capture the board's serial log to tools\monitor.log (no ESP-IDF terminal needed).
# Usage: capture.bat [COMx] [seconds]   - restarts the board first, like idf.py monitor, so the log
# starts with the bootloader (shows "BOOT held: back to MultiDeck" when the return button was seen).
import sys, time, os
import serial, serial.tools.list_ports

LIVE = os.environ.get("CAPTURE_LIVE") == "1"          # capture_live.py: no restart
here = os.path.dirname(os.path.abspath(__file__))
port = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].upper().startswith("COM") else None
secs = int(sys.argv[-1]) if len(sys.argv) > 1 and sys.argv[-1].isdigit() else 180
out = open(os.path.join(here, "monitor_live.log" if LIVE else "monitor.log"), "w", encoding="utf-8", errors="replace")
def log(s):
    out.write(s); out.flush(); print(s, end="")
ports = list(serial.tools.list_ports.comports())
for p in ports:
    log(f"# port {p.device}: {p.description} [{p.hwid}]\n")
if not port:
    pick = [p.device for p in ports if "1A86" in (p.hwid or "").upper() or "CH34" in (p.description or "").upper()]
    port = pick[0] if pick else "COM7"
log(f"# using {port} for {secs} s{' (live, no restart)' if LIVE else ''}\n")
try:
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
except Exception as e:
    log(f"# could not open {port}: {e}\n# (close any idf.py monitor window and try again)\n")
    sys.exit(1)
if not LIVE:
    s.rts = True; time.sleep(0.2); s.rts = False    # pulse EN: restart the board so the log starts at boot
t0, buf, n = time.time(), b"", 0
while time.time() - t0 < secs:
    d = s.read(4096)
    if not d:
        continue
    buf += d
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        log(f"[{time.time() - t0:7.2f}] " + line.decode("utf-8", "replace").rstrip("\r") + "\n")
        n += 1
log(f"# done, {n} lines\n")
