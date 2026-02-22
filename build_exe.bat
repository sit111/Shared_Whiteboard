@echo off
setlocal

where g++ >nul 2>nul
if %errorlevel% neq 0 (
  echo [ERROR] g++ not found. Install MinGW-w64 and add g++ to PATH.
  exit /b 1
)

echo Building Windows GUI SharedWhiteboard.exe ...
g++ -std=c++17 -O2 whiteboard_server.cpp -mwindows -lws2_32 -o SharedWhiteboard.exe
if %errorlevel% neq 0 (
  echo [ERROR] Build failed.
  exit /b 1
)

echo.
echo Build complete: SharedWhiteboard.exe
echo Start server: SharedWhiteboard.exe --server
echo Start GUI app: SharedWhiteboard.exe
endlocal
