# Shared Whiteboard (Single `.exe` Program, C++)

Program-only app (not a website). One executable runs either server or client.

## Build on Windows
```bat
build_exe.bat
```
Output: `SharedWhiteboard.exe`

## Run server (host PC)
```bat
SharedWhiteboard.exe --server --port 5050
```

## Run client (other PC)
```bat
SharedWhiteboard.exe --host <HOST_PC_IP> --port 5050 --room team1
```

## Rooms with and without passwords
- Open room (no password):
  - `create team1`
  - or auto-create/join with `room team1`
- Password-protected room:
  - `create secretroom mypass`
  - join later with `room secretroom mypass`

You can also use startup flags:
```bat
SharedWhiteboard.exe --host <HOST_PC_IP> --port 5050 --room secretroom --password mypass --create-room
```

## Client commands
- `draw x y [char]`
- `clear`
- `show`
- `room <name> [password]`
- `create <name> [password]`
- `state`
- `quit`

Board size: `60x20`.
