# tools

Double-click helpers for Windows, the same set as RigDeck's tools folder. They use the ESP-IDF 6.1 install in
`C:\Espressif` (as `start.bat`). Logs are written next to them and are not committed.

| | |
|---|---|
| `update_flash.bat` | `git pull`, build and flash MultiDeck over USB. Log: `flash.log` |
| `release.bat` | `git pull`, then `release.ps1`: `docs\firmware\multideck.bin`, `otadata.bin`, `manifest.json` for the flasher page. Log: `release.log` |
| `capture.bat [COMx] [seconds]` | restarts the board and saves its serial log for 3 minutes to `monitor.log` |
| `capture_10min.bat` | the same for 10 minutes |
| `capture_live.bat [COMx] [seconds]` | saves the log without restarting the board (`monitor_live.log`), e.g. while a firmware started from MultiDeck runs |

The board's USB-C serial port (CH343) is found by itself; otherwise give it, e.g. `capture.bat COM7`.
Both `update_flash.bat` and `release.bat` throw away local changes to tracked files (`git checkout -- .`)
before pulling, as RigDeck's do: edit on GitHub or commit first.
