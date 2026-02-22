# Shared Whiteboard (Windows GUI App, Single `.exe`)

You were right: now this is a **real Windows app window**, not terminal UI.

## What opens now
- A GUI window with:
  - Host / Port / Room / Password fields
  - **Join**, **Create**, **Clear** buttons
  - Click-and-draw board area
- Press `P` to change pen (`#` / `@`).

## Run server
```bat
SharedWhiteboard.exe --server
```
(Hosts on port `5050`.)

## Run client app window
```bat
SharedWhiteboard.exe
```
Then use Join/Create in the window.

## Rooms with and without password
- Create open room: leave Password empty + click **Create**
- Create locked room: set Password + click **Create**
- Join locked room: enter same Password + click **Join**

## Build on Windows (MinGW-w64)
```bat
build_exe.bat
```
Output: `SharedWhiteboard.exe`
