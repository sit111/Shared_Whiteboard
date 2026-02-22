@echo off
setlocal

where g++ >nul 2>nul
if %errorlevel% neq 0 (
  echo [ERROR] g++ not found. Install MinGW-w64 and add g++ to PATH.
  exit /b 1
)

echo Building Windows SharedWhiteboard.exe ...
g++ -std=c++17 -O2 -pthread whiteboard_server.cpp -lws2_32 -o SharedWhiteboard.exe
if %errorlevel% neq 0 (
  echo [ERROR] Build failed.
  exit /b 1
)

echo.
echo Build complete: SharedWhiteboard.exe
echo Run server: SharedWhiteboard.exe --server --port 5050
echo Run UI client: SharedWhiteboard.exe --host 127.0.0.1 --port 5050 --room main
echo Password room: SharedWhiteboard.exe --host 127.0.0.1 --port 5050 --room vip --password secret --create-room
endlocal
