const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');

const PORT = process.env.PORT || 3000;
const PUBLIC_DIR = path.join(__dirname, 'public');

const state = {
  strokes: []
};

const clients = new Set();

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8'
};

function broadcast(event, payload) {
  const data = `event: ${event}\ndata: ${JSON.stringify(payload)}\n\n`;
  for (const client of clients) {
    client.write(data);
  }
}

function collectJson(req, callback) {
  let body = '';
  req.on('data', (chunk) => {
    body += chunk;
  });
  req.on('end', () => {
    try {
      callback(null, JSON.parse(body || '{}'));
    } catch (error) {
      callback(error);
    }
  });
}

const server = http.createServer((req, res) => {
  if (req.method === 'GET' && req.url === '/events') {
    res.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache',
      Connection: 'keep-alive'
    });
    res.write('event: sync\n');
    res.write(`data: ${JSON.stringify({ strokes: state.strokes })}\n\n`);

    clients.add(res);
    req.on('close', () => {
      clients.delete(res);
    });
    return;
  }

  if (req.method === 'GET' && req.url === '/state') {
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify(state));
    return;
  }

  if (req.method === 'POST' && req.url === '/draw') {
    collectJson(req, (error, message) => {
      if (error || !Array.isArray(message.stroke?.points)) {
        res.writeHead(400);
        res.end('Invalid stroke');
        return;
      }

      state.strokes.push(message.stroke);
      broadcast('draw', { stroke: message.stroke });
      res.writeHead(204);
      res.end();
    });
    return;
  }

  if (req.method === 'POST' && req.url === '/clear') {
    state.strokes = [];
    broadcast('clear', {});
    res.writeHead(204);
    res.end();
    return;
  }

  if (req.method !== 'GET') {
    res.writeHead(405);
    res.end('Method not allowed');
    return;
  }

  const safePath = req.url === '/' ? 'index.html' : path.normalize(req.url).replace(/^\/+/, '');
  const filePath = path.join(PUBLIC_DIR, safePath);

  if (!filePath.startsWith(PUBLIC_DIR)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }

  fs.readFile(filePath, (error, data) => {
    if (error) {
      res.writeHead(404);
      res.end('Not found');
      return;
    }

    const ext = path.extname(filePath);
    res.writeHead(200, { 'Content-Type': MIME_TYPES[ext] || 'application/octet-stream' });
    res.end(data);
  });
});

server.listen(PORT, () => {
  console.log(`Shared whiteboard running at http://localhost:${PORT}`);
});
