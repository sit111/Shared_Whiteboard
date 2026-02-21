const canvas = document.getElementById('board');
const ctx = canvas.getContext('2d');
const colorPicker = document.getElementById('colorPicker');
const sizePicker = document.getElementById('sizePicker');
const clearButton = document.getElementById('clearButton');
const statusLabel = document.getElementById('status');

let drawing = false;
let currentStroke = null;

function resizeCanvasToDisplaySize() {
  const ratio = window.devicePixelRatio || 1;
  const width = Math.floor(canvas.clientWidth * ratio);
  const height = Math.floor(canvas.clientHeight * ratio);

  if (width === canvas.width && height === canvas.height) {
    return;
  }

  const snapshot = canvas.width > 0 && canvas.height > 0
    ? ctx.getImageData(0, 0, canvas.width, canvas.height)
    : null;

  canvas.width = width;
  canvas.height = height;

  if (snapshot) {
    ctx.putImageData(snapshot, 0, 0);
  }
}

function drawStroke(stroke) {
  if (!stroke.points || stroke.points.length < 2) {
    return;
  }

  ctx.strokeStyle = stroke.color;
  ctx.lineWidth = stroke.size;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';

  ctx.beginPath();
  ctx.moveTo(stroke.points[0].x, stroke.points[0].y);

  for (let i = 1; i < stroke.points.length; i += 1) {
    ctx.lineTo(stroke.points[i].x, stroke.points[i].y);
  }

  ctx.stroke();
}

function clearBoard() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
}

function getCanvasPoint(event) {
  const rect = canvas.getBoundingClientRect();
  const ratioX = canvas.width / rect.width;
  const ratioY = canvas.height / rect.height;

  return {
    x: (event.clientX - rect.left) * ratioX,
    y: (event.clientY - rect.top) * ratioY
  };
}

async function sendJson(url, payload = {}) {
  await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
}

const events = new EventSource('/events');

events.addEventListener('open', () => {
  statusLabel.textContent = 'Connected';
});

events.addEventListener('error', () => {
  statusLabel.textContent = 'Reconnecting...';
});

events.addEventListener('sync', (event) => {
  const message = JSON.parse(event.data);
  clearBoard();
  message.strokes.forEach(drawStroke);
});

events.addEventListener('draw', (event) => {
  const message = JSON.parse(event.data);
  drawStroke(message.stroke);
});

events.addEventListener('clear', () => {
  clearBoard();
});

clearButton.addEventListener('click', () => {
  sendJson('/clear').catch(() => {
    statusLabel.textContent = 'Send failed';
  });
});

canvas.addEventListener('pointerdown', (event) => {
  drawing = true;
  canvas.setPointerCapture(event.pointerId);

  currentStroke = {
    color: colorPicker.value,
    size: Number(sizePicker.value),
    points: [getCanvasPoint(event)]
  };
});

canvas.addEventListener('pointermove', (event) => {
  if (!drawing || !currentStroke) {
    return;
  }

  currentStroke.points.push(getCanvasPoint(event));
  drawStroke({ ...currentStroke, points: currentStroke.points.slice(-2) });
});

canvas.addEventListener('pointerup', (event) => {
  if (!drawing || !currentStroke) {
    return;
  }

  drawing = false;
  canvas.releasePointerCapture(event.pointerId);

  if (currentStroke.points.length > 1) {
    sendJson('/draw', { stroke: currentStroke }).catch(() => {
      statusLabel.textContent = 'Send failed';
    });
  }

  currentStroke = null;
});

window.addEventListener('resize', resizeCanvasToDisplaySize);
resizeCanvasToDisplaySize();
