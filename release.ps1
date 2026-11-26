# Build a MultiDeck release: one merged .bin for the browser flasher (docs/) and for esptool.
#
# Run it from the ESP-IDF 6.1 PowerShell (start.bat) in the MultiDeck folder:
#     .\release.ps1
# Change version.txt first for a new version number.
#
# It writes:
#   build\multideck-<version>.bin    bootloader + partition table + MultiDeck, for address 0x0
#   docs\firmware\multideck.bin      the same file, served by the flasher page
#   docs\firmware\otadata.bin        blank slot selector (0x7C0000): after installing, MultiDeck starts
#   docs\manifest.json               tells the flasher page which files and version
# The image ends after MultiDeck (factory partition), so the settings, log and Wi-Fi chip partitions of
# RigDeck, and the firmware already in the app slots, are not touched when it is written.

$ErrorActionPreference = "Continue"   # idf.py and esptool write progress to stderr; failures are checked by exit code
Set-Location -Path $PSScriptRoot
$ver = (Get-Content version.txt -TotalCount 1).Trim()
Write-Host "Building MultiDeck $ver ..." -ForegroundColor Cyan

idf.py build
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$out = "multideck-$ver.bin"
Push-Location build
try {
    # flash_args lists every part with its address. otadata sits far up the flash (0x7C0000): merging it
    # would pad the image over RigDeck's storage partitions. It goes to the flasher as a separate part.
    $args = Get-Content flash_args | Where-Object { $_ -notmatch "ota_data_initial" }
    [System.IO.File]::WriteAllLines((Join-Path (Get-Location) "flash_args_merge"), $args)
    python -m esptool --chip esp32p4 merge-bin -o $out "@flash_args_merge"
    if ($LASTEXITCODE -ne 0) { throw "merge-bin failed" }
} finally { Pop-Location }

New-Item -ItemType Directory -Force -Path docs\firmware | Out-Null
Copy-Item -Force "build\$out" docs\firmware\multideck.bin
if (-not (Test-Path build\ota_data_initial.bin)) { throw "build\ota_data_initial.bin is missing" }
Copy-Item -Force build\ota_data_initial.bin docs\firmware\otadata.bin

$manifest = @"
{
  "name": "MultiDeck",
  "version": "$ver",
  "new_install_prompt_erase": true,
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "ESP32-P4",
      "parts": [
        { "path": "firmware/multideck.bin", "offset": 0 },
        { "path": "firmware/otadata.bin", "offset": 8126464 }
      ]
    }
  ]
}
"@
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot "docs\manifest.json"), $manifest)   # UTF-8, no BOM

$size = (Get-Item "build\$out").Length
Write-Host ""
Write-Host "Done: build\$out ($([math]::Round($size/1MB,2)) MB), copied to docs\firmware\multideck.bin" -ForegroundColor Green
Write-Host ""
Write-Host "By hand with esptool instead of the flasher page:"
Write-Host "    python -m esptool --chip esp32p4 write-flash 0x0 build\$out 0x7C0000 build\ota_data_initial.bin"
