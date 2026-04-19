@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ENTRY=%SCRIPT_DIR%generate_appvars.py"
set "OUT_DIR=%SCRIPT_DIR%build"
set "CEDEV_CONVBIN=C:\CEdev\bin\convbin.exe"

if not exist "%ENTRY%" (
  echo [ERROR] entry script not found: %ENTRY%
  exit /b 1
)

if not exist "%CEDEV_CONVBIN%" (
  echo [ERROR] convbin not found: %CEDEV_CONVBIN%
  echo [ERROR] install CEdev first, or edit CEDEV_CONVBIN in this script.
  exit /b 1
)

echo [1/2] Installing Nuitka toolchain...
python -m pip install --upgrade nuitka ordered-set zstandard
if errorlevel 1 exit /b 1

echo [2/2] Building single-file executable with bundled convbin.exe...
python -m nuitka ^
  --onefile ^
  --assume-yes-for-downloads ^
  --enable-plugin=tk-inter ^
  --windows-console-mode=disable ^
  --output-dir="%OUT_DIR%" ^
  --include-data-file="%CEDEV_CONVBIN%=convbin.exe" ^
  "%ENTRY%"
if errorlevel 1 exit /b 1

echo.
echo Build complete.
echo Output: %OUT_DIR%\generate_appvars.exe
echo Run:    %OUT_DIR%\generate_appvars.exe

endlocal
