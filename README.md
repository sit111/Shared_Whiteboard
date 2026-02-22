# Shared Whiteboard (Windows GUI App, Single `.exe`)

Now this is a proper **Windows desktop GUI** (not terminal) with improved layout and working room controls.

## What is fixed
- Better UI design (toolbar + larger drawing area)
- Room actions are explicit and usable from buttons:
  - **Join Room**
  - **Create Room**
- Password and no-password rooms both supported from the same UI
- Clear room button works for the active room

## Run server
```bat
SharedWhiteboard.exe --server --port 5050
```

## Run client GUI
```bat
SharedWhiteboard.exe
```

In the window:
1. Set Host/Port
2. Enter Room and optional Password
3. Click **Create Room** (new room) or **Join Room** (existing room)
4. Draw with mouse drag

## Controls
- Mouse drag: draw
- **Clear Room** button: clear active room
- **Pen** button or key `P`: switch pen `#`/`@`

## Build on Windows (MinGW-w64)
```bat
build_exe.bat
```
Output: `SharedWhiteboard.exe`
