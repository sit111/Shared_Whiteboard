# Shared Whiteboard (Single Program `.exe`, C++)

You asked for **one program**. This version is exactly that:
- One source file: `whiteboard_server.cpp`
- One output program: `SharedWhiteboard.exe`
- No external `public/` files needed at runtime (HTML/CSS/JS are embedded in the EXE)

## Build on Windows
Run:
```bat
build_exe.bat
```

Output:
- `SharedWhiteboard.exe`

## Run
```bat
SharedWhiteboard.exe 8080
```

Then people connect from their PCs in a browser:
- `http://<HOST_PC_IP>:8080`

## Rooms
- Type a room name and click **Join**.
- Each room has isolated board state.
- `Clear Room` only clears the active room.

## Build on Linux/macOS (for testing)
```bash
g++ -std=c++17 -O2 -pthread whiteboard_server.cpp -o shared_whiteboard
./shared_whiteboard 8080
```
