@echo off
:: ============================================================
:: W17PAD (sk32/kb17) one-click build + flash
::  1. builds the firmware with qmk (MSYS MINGW64)
::  2. waits for the sk32duino DFU device (1EAF:0003)
::  3. flashes the app image on DFU alternative 2
::
:: Enter DFU mode: hold key [0][0] (PB2 x PB12) and reset the board,
:: then run this batch again (or now, while it is polling).
:: ============================================================
setlocal
cd /d "%~dp0..\..\.."   :: repo root (keyboards\sk32\kb17 -> qmk_firmware)

set "DFU_TOOL=D:\QMK_MSYS\mingw64\bin\dfu-util.exe"
set "BASHEXE=D:\QMK_MSYS\usr\bin\bash.exe"
set "BIN=.build\sk32_kb17_default.bin"

if not exist "%DFU_TOOL%" (
  echo [ERROR] dfu-util not found: %DFU_TOOL%
  exit /b 1
)
if not exist "%BASHEXE%" (
  echo [ERROR] MSYS bash not found: %BASHEXE%
  exit /b 1
)

echo === Building sk32/kb17 default ... ===
set "MSYSTEM=MINGW64"
"%BASHEXE%" --login -lc "cd /c/Users/Administrator/Desktop/qmk/qmk_firmware && qmk compile -kb sk32/kb17 -km default"
if errorlevel 1 (
  echo [ERROR] Build failed.
  exit /b 1
)

echo.
echo === Waiting for DFU device 1EAF:0003 (hold [0][0] + reset) ... ===
powershell -NoProfile -Command "$t=(Get-Date).AddSeconds(45); $d=$null; do { $d=Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -match 'VID_1EAF&PID_0003' }; if ($d) { break }; Start-Sleep -Milliseconds 400 } while ((Get-Date) -lt $t); if (-not $d) { Write-Error 'DFU device not found'; exit 1 }"
if errorlevel 1 (
  echo.
  echo [ERROR] DFU device 1EAF:0003 not found in 45s.
  echo Try: hold key [0][0] and press/reset the board, then rerun.
  exit /b 1
)

echo === Flashing app image (DFU alt 2) ... ===
"%DFU_TOOL%" -a 2 -D "%BIN%"
if errorlevel 1 (
  echo [ERROR] dfu-util failed to flash.
  exit /b 1
)

echo.
echo [OK] Flashed. Board will re-enumerate as a HID keyboard.
exit /b 0