# Shared Whiteboard (ONE-FILE EXE, NON-LOCAL READY)

Source code is one file:
- `whiteboard_app.py`

Output is one executable file:
- `SharedWhiteboard.exe`

## Non-local usage (remote server)

Run one machine as the server (VPS/cloud/remote PC):

```bash
SharedWhiteboard.exe --server --host 0.0.0.0 --port 5050
```

Then clients connect from other machines:

```bash
SharedWhiteboard.exe --host <SERVER_PUBLIC_IP> --port 5050
```

## Build `.exe` on Windows

```bat
build_exe.bat
```

Build output:
- `dist\SharedWhiteboard.exe`

## Build `.exe` on GitHub (not local)

Workflow:
- `.github/workflows/build-windows-exe.yml`

Steps:
1. Push this repo to GitHub.
2. Open **Actions** → **Build Windows EXE**.
3. Run workflow.
4. Download `SharedWhiteboard-exe` artifact.

## Run from source (optional)

Server mode:
```bash
python3 whiteboard_app.py --server --host 0.0.0.0 --port 5050
```

Client mode:
```bash
python3 whiteboard_app.py --host <SERVER_IP> --port 5050
```
