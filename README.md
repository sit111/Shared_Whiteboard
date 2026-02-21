# Shared Whiteboard (ONE-FILE EXE)

Source code is a single file:
- `whiteboard_app.py`

Target output is a single file executable:
- `SharedWhiteboard.exe`

## Build `.exe` locally on Windows

```bat
build_exe.bat
```

This builds:
- `dist\SharedWhiteboard.exe`

## Build `.exe` on GitHub (no local setup)

This repo includes a GitHub Actions workflow:
- `.github/workflows/build-windows-exe.yml`

How to use:
1. Push code to your GitHub repo.
2. Open **Actions** → **Build Windows EXE**.
3. Run workflow.
4. Download artifact `SharedWhiteboard-exe`.

## Run from source (optional)

```bash
python3 whiteboard_app.py
```
