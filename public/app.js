const board = document.getElementById('board');
const ctx = board.getContext('2d');
const statusEl = document.getElementById('status');
const roomInput = document.getElementById('room');
const joinBtn = document.getElementById('join');
const colorInput = document.getElementById('color');
const sizeInput = document.getElementById('size');
const clearBtn = document.getElementById('clear');

let room = roomInput.value.trim() || 'main';
let eventSource;
let drawing = false;
let points = [];

function resize() {
  board.width = window.innerWidth;
  board.height = window.innerHeight - document.querySelector('header').offsetHeight;
}

window.addEventListener('resize', resize);
resize();

function drawStroke(stroke) {
  if (!stroke?.points?.length) return;
  ctx.strokeStyle = stroke.color || '#111';
  ctx.lineWidth = stroke.size || 3;
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(stroke.points[0].x, stroke.points[0].y);
  for (let i = 1; i < stroke.points.length; i++) {
    ctx.lineTo(stroke.points[i].x, stroke.points[i].y);
  }
  ctx.stroke();
}

function connect() {
  if (eventSource) eventSource.close();
  room = roomInput.value.trim() || 'main';
  eventSource = new EventSource(`/events?room=${encodeURIComponent(room)}`);

  eventSource.addEventListener('open', () => { statusEl.textContent = `connected:${room}`; });
  eventSource.addEventListener('error', () => { statusEl.textContent = 'reconnecting...'; });

  eventSource.addEventListener('sync', (event) => {
    const payload = JSON.parse(event.data);
    ctx.clearRect(0, 0, board.width, board.height);
    (payload.strokes || []).forEach(drawStroke);
  });

  eventSource.addEventListener('draw', (event) => {
    const payload = JSON.parse(event.data);
    if (payload.room === room) drawStroke(payload.stroke);
  });

  eventSource.addEventListener('clear', (event) => {
    const payload = JSON.parse(event.data);
    if (payload.room === room) ctx.clearRect(0, 0, board.width, board.height);
  });
}

joinBtn.addEventListener('click', connect);

function post(path, payload) {
  return fetch(`${path}?room=${encodeURIComponent(room)}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
}

board.addEventListener('pointerdown', (e) => {
  drawing = true;
  points = [{ x: e.offsetX, y: e.offsetY }];
});

board.addEventListener('pointermove', (e) => {
  if (!drawing) return;
  points.push({ x: e.offsetX, y: e.offsetY });
  drawStroke({ color: colorInput.value, size: Number(sizeInput.value), points: points.slice(-2) });
});

board.addEventListener('pointerup', async () => {
  if (!drawing) return;
  drawing = false;
  await post('/draw', { color: colorInput.value, size: Number(sizeInput.value), points });
  points = [];
});

clearBtn.addEventListener('click', async () => {
  await post('/clear', {});
});

connect();
