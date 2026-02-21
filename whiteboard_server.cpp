#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> running(true);

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

std::string urlDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            std::string hex = text.substr(i + 1, 2);
            char c = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            out.push_back(c);
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
        size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        params[urlDecode(item.substr(0, eq))] = urlDecode(item.substr(eq + 1));
    }
    return params;
}

struct RoomState {
    std::vector<std::string> strokes;
    std::vector<int> sseClients;
};

std::mutex roomsMutex;
std::map<std::string, RoomState> rooms;

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
        ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string httpResponse(const std::string& status, const std::string& contentType, const std::string& body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

std::string loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

void broadcastSseLocked(const std::string& room, const std::string& event, const std::string& dataJson) {
    std::string payload = "event: " + event + "\n";
    payload += "data: " + dataJson + "\n\n";

    auto& clients = rooms[room].sseClients;
    std::vector<int> alive;
    for (int client : clients) {
        if (sendAll(client, payload)) {
            alive.push_back(client);
        } else {
            close(client);
        }
    }
    clients.swap(alive);
}

std::string normalizeRoom(const std::string& room) {
    std::string normalized = trim(room);
    if (normalized.empty()) return "main";
    if (normalized.size() > 64) normalized.resize(64);
    return normalized;
}

void handleSse(int clientSock, const std::string& room) {
    std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n\r\n";
    if (!sendAll(clientSock, header)) {
        close(clientSock);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(roomsMutex);
        rooms[room].sseClients.push_back(clientSock);
        std::ostringstream sync;
        sync << "{\"room\":\"" << jsonEscape(room) << "\",\"strokes\":[";
        for (size_t i = 0; i < rooms[room].strokes.size(); ++i) {
            if (i > 0) sync << ',';
            sync << rooms[room].strokes[i];
        }
        sync << "]}";
        broadcastSseLocked(room, "sync", sync.str());
    }
}

void handleClient(int clientSock) {
    std::string request;
    char buffer[8192];
    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(clientSock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            close(clientSock);
            return;
        }
        request.append(buffer, n);
        if (request.size() > 1024 * 1024) {
            close(clientSock);
            return;
        }
    }

    size_t headerEnd = request.find("\r\n\r\n");
    std::string headerText = request.substr(0, headerEnd);
    std::string body = request.substr(headerEnd + 4);

    std::stringstream headers(headerText);
    std::string requestLine;
    std::getline(headers, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

    std::string method, path, version;
    std::stringstream rl(requestLine);
    rl >> method >> path >> version;

    std::map<std::string, std::string> headerMap;
    std::string line;
    size_t contentLength = 0;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        headerMap[key] = value;
    }
    if (headerMap.count("content-length")) {
        contentLength = std::stoul(headerMap["content-length"]);
    }
    while (body.size() < contentLength) {
        ssize_t n = recv(clientSock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        body.append(buffer, n);
    }

    std::string query;
    size_t qPos = path.find('?');
    if (qPos != std::string::npos) {
        query = path.substr(qPos + 1);
        path = path.substr(0, qPos);
    }
    auto params = parseQuery(query);
    std::string room = normalizeRoom(params.count("room") ? params["room"] : "main");

    if (method == "GET" && path == "/") {
        std::string html = loadFile("public/index.html");
        if (html.empty()) {
            sendAll(clientSock, httpResponse("500 Internal Server Error", "text/plain", "Missing public/index.html"));
        } else {
            sendAll(clientSock, httpResponse("200 OK", "text/html; charset=utf-8", html));
        }
        close(clientSock);
        return;
    }

    if (method == "GET" && path == "/app.js") {
        std::string js = loadFile("public/app.js");
        sendAll(clientSock, httpResponse("200 OK", "application/javascript; charset=utf-8", js));
        close(clientSock);
        return;
    }

    if (method == "GET" && path == "/styles.css") {
        std::string css = loadFile("public/styles.css");
        sendAll(clientSock, httpResponse("200 OK", "text/css; charset=utf-8", css));
        close(clientSock);
        return;
    }

    if (method == "GET" && path == "/events") {
        handleSse(clientSock, room);
        return;
    }

    if (method == "GET" && path == "/state") {
        std::ostringstream out;
        std::lock_guard<std::mutex> lock(roomsMutex);
        out << "{\"room\":\"" << jsonEscape(room) << "\",\"strokes\":[";
        auto& strokes = rooms[room].strokes;
        for (size_t i = 0; i < strokes.size(); ++i) {
            if (i) out << ',';
            out << strokes[i];
        }
        out << "]}";
        sendAll(clientSock, httpResponse("200 OK", "application/json", out.str()));
        close(clientSock);
        return;
    }

    if (method == "POST" && path == "/draw") {
        {
            std::lock_guard<std::mutex> lock(roomsMutex);
            rooms[room].strokes.push_back(body);
            std::ostringstream event;
            event << "{\"room\":\"" << jsonEscape(room) << "\",\"stroke\":" << body << "}";
            broadcastSseLocked(room, "draw", event.str());
        }
        sendAll(clientSock, httpResponse("200 OK", "application/json", "{\"ok\":true}"));
        close(clientSock);
        return;
    }

    if (method == "POST" && path == "/clear") {
        {
            std::lock_guard<std::mutex> lock(roomsMutex);
            rooms[room].strokes.clear();
            std::ostringstream event;
            event << "{\"room\":\"" << jsonEscape(room) << "\"}";
            broadcastSseLocked(room, "clear", event.str());
        }
        sendAll(clientSock, httpResponse("200 OK", "application/json", "{\"ok\":true}"));
        close(clientSock);
        return;
    }

    sendAll(clientSock, httpResponse("404 Not Found", "text/plain", "Not found"));
    close(clientSock);
}

void signalHandler(int) {
    running.store(false);
}

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

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

    std::cout << "C++ shared whiteboard server running on port " << port << "\n";

    while (running.load()) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(serverSock, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientSock < 0) {
            if (running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        std::thread(handleClient, clientSock).detach();
    }

    close(serverSock);
    return 0;
}
