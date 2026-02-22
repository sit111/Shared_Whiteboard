# Shared Whiteboard (Single `.exe` Program, C++)

This is a **program UI** version (not website). One executable runs server or interactive client UI.

## Build on Windows
```bat
build_exe.bat
```
Output: `SharedWhiteboard.exe`

## Run server
```bat
SharedWhiteboard.exe --server --port 5050
```

## Run UI client
```bat
SharedWhiteboard.exe --host <HOST_PC_IP> --port 5050 --room team1
```

## Rooms with/without passwords
- Create open room: press `N`, room name, empty password
- Create locked room: press `N`, room name, set password
- Join room: press `J`, room name, enter password if needed
- CLI startup flags also work:
```bat
SharedWhiteboard.exe --host <HOST_PC_IP> --port 5050 --room vip --password secret --create-room
```

## UI controls
- `W/A/S/D`: move cursor
- `Space`: draw with current pen
- `P`: toggle pen char (`#` / `@`)
- `X`: clear room
- `J`: join room (prompt)
- `N`: create room (prompt)
- `Q`: quit
