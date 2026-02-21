# Shared Whiteboard (Desktop Source Code)

This repository now provides a **desktop shared whiteboard program** (not a web app).

It contains:
- `whiteboard_server.py` - TCP server that keeps board state and broadcasts updates
- `whiteboard_client.py` - Tkinter desktop whiteboard client GUI

## Features

- Real-time shared drawing across multiple desktop clients
- Brush color picker and brush size slider
- Clear board synchronization for all connected users
- Uses only Python standard library (no third-party runtime dependencies)

## Run

### 1) Start the server

```bash
python3 whiteboard_server.py
```

### 2) Start one or more clients

```bash
python3 whiteboard_client.py
```

In each client, enter server host/port and click **Connect**.

## Build as `.exe` (Windows)

On Windows (with Python installed), you can package the client/server into executables:

```bash
pip install pyinstaller
pyinstaller --onefile --windowed whiteboard_client.py
pyinstaller --onefile whiteboard_server.py
```

Generated executables will be under `dist/`.
