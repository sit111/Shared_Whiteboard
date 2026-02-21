#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

const char* kHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Shared Whiteboard</title>
  <style>
    body { margin: 0; font-family: Arial, sans-serif; }
    header { background: #222; color: #fff; padding: 8px 12px; }
    .controls { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
    canvas { display: block; width: 100vw; height: calc(100vh - 64px); background: #fff; cursor: crosshair; }
    button { padding: 6px 10px; }
  </style>
</head>
<body>
  <header>
    <h1>Shared Whiteboard (Single EXE)</h1>
    <div class="controls">
      <label>Room <input id="room" value="main" /></label>
      <button id="join">Join</button>
      <label>Color <input id="color" type="color" value="#111111" /></label>
      <label>Size <input id="size" type="range" min="1" max="20" value="3" /></label>
      <button id="clear">Clear Room</button>
      <span id="status">disconnected</span>
    </div>
  </header>
  <canvas id="board"></canvas>
  <script>
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

    function post(path, payload) {
      return fetch(`${path}?room=${encodeURIComponent(room)}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
    }

    joinBtn.addEventListener('click', connect);
    clearBtn.addEventListener('click', () => post('/clear', {}));

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

    window.addEventListener('resize', resize);
    resize();
    connect();
  </script>
</body>
</html>)HTML";

std::atomic<bool> running(true);

struct RoomState {
    std::vector<std::string> strokes;
    std::vector<int> sseClients;
};

std::mutex roomsMutex;
std::map<std::string, RoomState> rooms;

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

std::string normalizeRoom(const std::string& room) {
    std::string normalized = trim(room);
    if (normalized.empty()) return "main";
    if (normalized.size() > 64) normalized.resize(64);
    return normalized;
}

std::string urlDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const std::string hex = text.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
            i += 2;
        } else if (text[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::map<std::string, std::string> parseQuery(const std::string& query) {
    std::map<std::string, std::string> params;
    std::stringstream ss(query);
    std::string item;
    while (std::getline(ss, item, '&')) {
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        params[urlDecode(item.substr(0, eq))] = urlDecode(item.substr(eq + 1));
    }
    return params;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

bool sendAll(int sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string httpResponse(const std::string& status, const std::string& contentType, const std::string& body,
                         const std::string& extraHeaders = "") {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << extraHeaders
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

void broadcastSseLocked(const std::string& room, const std::string& event, const std::string& dataJson) {
    const std::string payload = "event: " + event + "\n" + "data: " + dataJson + "\n\n";
    auto& clients = rooms[room].sseClients;
    std::vector<int> alive;
    alive.reserve(clients.size());
    for (const int client : clients) {
        if (sendAll(client, payload)) {
            alive.push_back(client);
        } else {
            close(client);
        }
    }
    clients.swap(alive);
}

void handleSse(int clientSock, const std::string& room) {
    const std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n\r\n";
    if (!sendAll(clientSock, header)) {
        close(clientSock);
        return;
    }

    std::lock_guard<std::mutex> lock(roomsMutex);
    rooms[room].sseClients.push_back(clientSock);

    std::ostringstream sync;
    sync << "{\"room\":\"" << jsonEscape(room) << "\",\"strokes\":[";
    const auto& strokes = rooms[room].strokes;
    for (size_t i = 0; i < strokes.size(); ++i) {
        if (i) sync << ',';
        sync << strokes[i];
    }
    sync << "]}";
    broadcastSseLocked(room, "sync", sync.str());
}

void handleClient(int clientSock) {
    std::string request;
    char buffer[8192];
    while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = recv(clientSock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            close(clientSock);
            return;
        }
        request.append(buffer, static_cast<size_t>(n));
        if (request.size() > 1024 * 1024) {
            close(clientSock);
            return;
        }
    }

    const size_t headerEnd = request.find("\r\n\r\n");
    const std::string headerText = request.substr(0, headerEnd);
    std::string body = request.substr(headerEnd + 4);

    std::stringstream headers(headerText);
    std::string requestLine;
    std::getline(headers, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

    std::string method, path, version;
    std::stringstream rl(requestLine);
    rl >> method >> path >> version;

    std::map<std::string, std::string> headerMap;
    size_t contentLength = 0;
    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        headerMap[key] = value;
    }

    if (headerMap.count("content-length")) {
        contentLength = static_cast<size_t>(std::stoul(headerMap["content-length"]));
    }
    while (body.size() < contentLength) {
        const ssize_t n = recv(clientSock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        body.append(buffer, static_cast<size_t>(n));
    }

    std::string query;
    const size_t qPos = path.find('?');
    if (qPos != std::string::npos) {
        query = path.substr(qPos + 1);
        path = path.substr(0, qPos);
    }
    const auto params = parseQuery(query);
    const std::string room = normalizeRoom(params.count("room") ? params.at("room") : "main");

    if (method == "GET" && path == "/") {
        sendAll(clientSock, httpResponse("200 OK", "text/html; charset=utf-8", kHtml));
        close(clientSock);
        return;
    }

    if (method == "GET" && path == "/state") {
        std::ostringstream out;
        std::lock_guard<std::mutex> lock(roomsMutex);
        out << "{\"room\":\"" << jsonEscape(room) << "\",\"strokes\":[";
        const auto& strokes = rooms[room].strokes;
        for (size_t i = 0; i < strokes.size(); ++i) {
            if (i) out << ',';
            out << strokes[i];
        }
        out << "]}";
        sendAll(clientSock, httpResponse("200 OK", "application/json", out.str()));
        close(clientSock);
        return;
    }

    if (method == "GET" && path == "/events") {
        handleSse(clientSock, room);
        return;
    }

    if (method == "POST" && path == "/draw") {
        std::lock_guard<std::mutex> lock(roomsMutex);
        rooms[room].strokes.push_back(body);
        std::ostringstream event;
        event << "{\"room\":\"" << jsonEscape(room) << "\",\"stroke\":" << body << "}";
        broadcastSseLocked(room, "draw", event.str());
        sendAll(clientSock, httpResponse("200 OK", "application/json", "{\"ok\":true}"));
        close(clientSock);
        return;
    }

    if (method == "POST" && path == "/clear") {
        std::lock_guard<std::mutex> lock(roomsMutex);
        rooms[room].strokes.clear();
        std::ostringstream event;
        event << "{\"room\":\"" << jsonEscape(room) << "\"}";
        broadcastSseLocked(room, "clear", event.str());
        sendAll(clientSock, httpResponse("200 OK", "application/json", "{\"ok\":true}"));
        close(clientSock);
        return;
    }

    sendAll(clientSock, httpResponse("404 Not Found", "text/plain", "Not found"));
    close(clientSock);
}

void signalHandler(int) { running.store(false); }

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);

    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    const int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(serverSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind on port " << port << "\n";
        close(serverSock);
        return 1;
    }

    if (listen(serverSock, 64) < 0) {
        std::cerr << "Failed to listen\n";
        close(serverSock);
        return 1;
    }

    std::cout << "SharedWhiteboard single-program server running on port " << port << "\n";

    while (running.load()) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        const int clientSock = accept(serverSock, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientSock < 0) {
            if (running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        std::thread(handleClient, clientSock).detach();
    }

    close(serverSock);
    return 0;
}
