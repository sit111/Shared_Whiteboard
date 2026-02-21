# Shared Whiteboard (Single `.exe` Program, C++)

This is now a **program-only** implementation (no website UI).

## What you get
- One source file: `whiteboard_server.cpp`
- One executable: `SharedWhiteboard.exe`
- Same EXE can run as server or client
- Room support (`--room`) so groups are separated

## Build on Windows
Run:
```bat
build_exe.bat
```

Output:
- `SharedWhiteboard.exe`

## Run as server (host PC)
```bat
SharedWhiteboard.exe --server --port 5050
```

## Run as client (other PC)
```bat
SharedWhiteboard.exe --host <HOST_PC_IP> --port 5050 --room team1
```

Client commands:
- `draw x y [char]`
- `clear`
- `show`
- `room <name>`
- `state`
- `quit`

Board size is `60x20`.

## Linux/macOS quick test
```bash
g++ -std=c++17 -O2 -pthread whiteboard_server.cpp -o shared_whiteboard
./shared_whiteboard --server --port 5050
```
