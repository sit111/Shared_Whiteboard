# Shared Whiteboard (C++)

This version uses **C++ (no Python)** for the server and serves a browser whiteboard UI with room-based collaboration.

## Features
- C++ server (`whiteboard_server.cpp`)
- Room isolation (`?room=team-a` etc.)
- Real-time updates via SSE (`draw`, `clear`, `sync`)
- Works across PCs: host once, others open browser to host IP

## Run on Linux/macOS (g++)
```bash
g++ -std=c++17 -O2 -pthread whiteboard_server.cpp -o shared_whiteboard
./shared_whiteboard 8080
```

Then open:
- `http://<SERVER_IP>:8080`

## Build `.exe` on Windows (MinGW g++)
Use:
```bat
build_exe.bat
```

Output:
- `SharedWhiteboard.exe`

Run:
```bat
SharedWhiteboard.exe 8080
```
Then share `http://<HOST_PC_IP>:8080` with other users.

## Project Files
- `whiteboard_server.cpp` - C++ HTTP/SSE room server
- `public/index.html` - whiteboard page
- `public/app.js` - drawing + sync client
- `public/styles.css` - UI styles
