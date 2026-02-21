@echo off
setlocal

where g++ >nul 2>nul
if %errorlevel% neq 0 (
  echo [ERROR] g++ not found. Install MinGW-w64 and add g++ to PATH.
  exit /b 1
)

echo Building SharedWhiteboard.exe from C++ source...
g++ -std=c++17 -O2 -pthread whiteboard_server.cpp -o SharedWhiteboard.exe
if %errorlevel% neq 0 (
  echo [ERROR] Build failed.
  exit /b 1
)

echo.
echo Build complete: SharedWhiteboard.exe
echo Run: SharedWhiteboard.exe 8080
endlocal
