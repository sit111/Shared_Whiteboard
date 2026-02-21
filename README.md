# Shared Whiteboard (One-File EXE + Rooms)

Single source file:
- `whiteboard_app.py`

Single output executable:
- `SharedWhiteboard.exe`

## What is improved

- Room-based collaboration (each room has isolated board state)
- Remote server mode for hosting on any PC/VPS
- GUI client mode for users joining a server + room

## Run server on host PC

```bash
SharedWhiteboard.exe --server --host 0.0.0.0 --port 5050
```

## Run client on user PC

```bash
SharedWhiteboard.exe --host <SERVER_PUBLIC_IP> --port 5050 --room team-a
```

Or set room from the GUI room field and click **Join Room**.

## Build `.exe` on Windows

```bat
build_exe.bat
```

Build output:
- `dist\SharedWhiteboard.exe`

## Build `.exe` on GitHub Actions

Workflow file:
- `.github/workflows/build-windows-exe.yml`

Steps:
1. Push to GitHub.
2. Open **Actions** → **Build Windows EXE**.
3. Run the workflow.
4. Download artifact `SharedWhiteboard-exe`.

## Run from source (optional)

Server mode:
```bash
python3 whiteboard_app.py --server --host 0.0.0.0 --port 5050
```

Client mode:
```bash
python3 whiteboard_app.py --host <SERVER_IP> --port 5050 --room team-a
```
