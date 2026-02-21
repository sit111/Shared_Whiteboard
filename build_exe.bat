@echo off
setlocal

where py >nul 2>nul
if %errorlevel% neq 0 (
  echo [ERROR] Python launcher (py) not found. Install Python for Windows first.
  exit /b 1
)

echo [1/3] Ensuring pip is available...
py -m ensurepip --upgrade >nul 2>nul

echo [2/3] Installing/upgrading build dependency: pyinstaller...
py -m pip install --upgrade pip pyinstaller
if %errorlevel% neq 0 (
  echo [ERROR] Failed to install pyinstaller.
  exit /b 1
)

echo [3/3] Building one-file Windows executable...
py -m PyInstaller --clean --noconfirm --onefile --windowed --name SharedWhiteboard whiteboard_app.py
if %errorlevel% neq 0 (
  echo [ERROR] Build failed.
  exit /b 1
)

echo.
echo Build complete.
echo EXE path: dist\SharedWhiteboard.exe
endlocal
