# Shared Whiteboard (Windows Single `.exe`, C++)

This is a Windows app (`SharedWhiteboard.exe`) with:
- Server mode
- Interactive UI client mode
- Rooms with optional passwords

## Why it was closing before
If client mode starts and no server is running, the app exits quickly. Now it shows a clear error and (on Windows) waits for a key before closing.

## Double-click launcher (Windows)
If you run `SharedWhiteboard.exe` with no args, it now shows a launcher:
1) Start Server
2) Start Client UI

## Build on Windows (MinGW-w64)
```bat
build_exe.bat
```
Output: `SharedWhiteboard.exe`

## Run directly with args
Server:
```bat
SharedWhiteboard.exe --server --port 5050
```
Client UI:
```bat
SharedWhiteboard.exe --host <HOST_PC_IP> --port 5050 --room team1
```

## Room password options
- `N` create room, set password or leave empty for open room
- `J` join room, enter password if locked

## UI controls
- `W/A/S/D`: move cursor
- `Space`: draw
- `X`: clear room
- `P`: switch pen char
- `J`: join room (prompt)
- `N`: create room (prompt)
- `Q`: quit
