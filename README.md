# Shared Whiteboard

A minimal real-time shared whiteboard app where everyone connected to the same server can draw on one shared canvas.

## Features

- Live drawing synchronization over Server-Sent Events + HTTP
- Brush color and brush size controls
- Board clear action synced to all connected users
- Lightweight Node.js backend (no framework required)

## Run locally

```bash
npm start
```

Then open `http://localhost:3000` in multiple tabs or devices on the same network.

## Project structure

- `server.js` - Static file server + SSE state sync
- `public/index.html` - Whiteboard UI
- `public/styles.css` - Whiteboard styling
- `public/app.js` - Canvas drawing + real-time socket client
