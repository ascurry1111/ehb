<#
  ============================================================================
  Electric Handbell v0.3 -- Batch-flash the XIAO ESP32C3 concurrency boards
  ============================================================================
  Compiles a sketch ONCE, then uploads the same compiled binary to every
  connected board in turn -- so flashing nine boards means plugging them all
  into a powered USB hub once (leave them there) and running this script,
  instead of connect/flash/disconnect/repeat nine times.

  USAGE
    From this folder or anywhere, e.g.:
      .\flash_all.ps1
      .\flash_all.ps1 -Sketch ..\xiao_c3_bringup
      .\flash_all.ps1 -Ports COM5,COM6,COM7

    With no -Ports, it auto-detects every currently connected serial port
    and tries to flash all of them. With no -Sketch, it defaults to
    xiao_c3_bringup (today's bring-up test sketch).

  WHY ONE COMPILE, MANY UPLOADS
    `arduino-cli compile` is the slow step (tens of seconds). `arduino-cli
    upload --input-dir` reuses that already-built binary and just does the
    esptool flash, which is a few seconds per board. For nine boards this is
    the difference between ~1 compile + 9 quick flashes vs. 9 full compiles.

  A FAILED BOARD DOESN'T STOP THE BATCH
    Each port is flashed independently and failures are collected and
    reported at the end, so one bad cable or a board still in a weird state
    doesn't block the other eight.

  IF A PORT UPLOAD HANGS OR FAILS
    The XIAO ESP32C3's native USB should auto-reset into the bootloader for
    upload. If a specific board won't take it: hold BOOT, tap RESET, release
    BOOT, then re-run the script with just that port via -Ports.
  ============================================================================
#>

param(
  [string]$Sketch = "$PSScriptRoot\..\xiao_c3_bringup",
  [string]$Fqbn = "esp32:esp32:XIAO_ESP32C3",
  [string[]]$Ports
)

$ErrorActionPreference = "Stop"

# Arduino IDE 2.x bundles arduino-cli and shares its board/core index with it,
# so this works with zero extra setup as long as the IDE's Boards Manager has
# the esp32 core installed (Tools > Board > Boards Manager > "esp32").
$CliCandidates = @(
  "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
  "arduino-cli"  # fall back to PATH if installed standalone
)
$Cli = $CliCandidates | Where-Object { (Get-Command $_ -ErrorAction SilentlyContinue) -or (Test-Path $_) } | Select-Object -First 1
if (-not $Cli) {
  Write-Error "Couldn't find arduino-cli. Expected it bundled with Arduino IDE, or on PATH."
  exit 1
}

$Sketch = (Resolve-Path $Sketch).Path
$BuildDir = Join-Path $env:TEMP "ehb_flash_build"
if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }

if (-not $Ports) {
  $Ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
}
if (-not $Ports -or $Ports.Count -eq 0) {
  Write-Error "No serial ports detected. Plug in the boards (via the USB hub) and try again."
  exit 1
}

Write-Output "Sketch:  $Sketch"
Write-Output "Board:   $Fqbn"
Write-Output "Ports:   $($Ports -join ', ')"
Write-Output ""

Write-Output "=== Compiling once ==="
& $Cli compile --fqbn $Fqbn $Sketch --output-dir $BuildDir
if ($LASTEXITCODE -ne 0) {
  Write-Error "Compile failed -- fix the sketch before flashing any boards."
  exit 1
}

$Failed = @()
$Succeeded = @()

foreach ($port in $Ports) {
  Write-Output ""
  Write-Output "=== Flashing $port ==="
  & $Cli upload -p $port --fqbn $Fqbn --input-dir $BuildDir
  if ($LASTEXITCODE -eq 0) {
    $Succeeded += $port
  } else {
    Write-Warning "Upload to $port failed (exit $LASTEXITCODE) -- continuing with remaining ports."
    $Failed += $port
  }
}

Write-Output ""
Write-Output "=== Summary ==="
Write-Output "Succeeded ($($Succeeded.Count)): $($Succeeded -join ', ')"
if ($Failed.Count -gt 0) {
  Write-Warning "Failed ($($Failed.Count)): $($Failed -join ', ')"
  exit 1
}
