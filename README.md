# Shared Whiteboard (Single-File Desktop Source)

This project is now a **single Python source file** desktop app:

- `whiteboard_app.py`

It includes both:
- embedded whiteboard server logic
- desktop GUI whiteboard client logic

So you can package it into **one `.exe` file**.

## Run from source

```bash
python3 whiteboard_app.py
```

In the app:
1. Choose host/port.
2. Click **Start Local Session** to host + auto-connect on the same machine.
3. Or click **Connect** to join an existing host.

## Build one-file `.exe` (includes runtime/dependencies)

On Windows:

```bash
pip install pyinstaller
pyinstaller --onefile --windowed --name SharedWhiteboard whiteboard_app.py
```

Output:
- `dist/SharedWhiteboard.exe`

That `.exe` is a single file and carries the Python runtime and required modules from this app.
