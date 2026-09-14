@echo off
:: Build sk32/ld7_oled default firmware only (no flash).
setlocal
cd /d "%~dp0..\..\.."   :: repo root

set "BASHEXE=D:\QMK_MSYS\usr\bin\bash.exe"
if not exist "%BASHEXE%" (
  echo [ERROR] MSYS bash not found: %BASHEXE%
  exit /b 1
)

echo === Building sk32/ld7_oled default ... ===
set "MSYSTEM=MINGW64"
"%BASHEXE%" --login -lc "cd /c/Users/Administrator/Desktop/qmk/qmk_firmware && qmk compile -kb sk32/ld7_oled -km default"
if errorlevel 1 (
  echo [ERROR] Build failed.
  exit /b 1
)

echo.
echo [OK] Build done: .build\sk32_ld7_oled_default.bin
exit /b 0