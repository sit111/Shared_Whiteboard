# Shared Whiteboard (Windows Single `.exe`, C++)

This is built for **Windows** as one program (`SharedWhiteboard.exe`) with:
- Server mode
- Interactive in-app UI client mode
- Rooms with optional passwords

## Build on Windows (MinGW-w64)
```bat
build_exe.bat
```
Output: `SharedWhiteboard.exe`

## Run server on Windows PC
```bat
SharedWhiteboard.exe --server --port 5050
```

## Run UI client on another Windows PC
```bat
SharedWhiteboard.exe --host <HOST_PC_IP> --port 5050 --room team1
```

## Room password options in UI
- `N` create room, set password or leave empty for open room
- `J` join room, enter password if room is locked

## UI controls
- `W/A/S/D`: move cursor
- `Space`: draw
- `X`: clear room
- `P`: switch pen char
- `J`: join room (prompt)
- `N`: create room (prompt)
- `Q`: quit

## CLI flags
- `--server`
- `--host <ip>`
- `--port <num>`
- `--room <name>`
- `--password <pwd>`
- `--create-room`
