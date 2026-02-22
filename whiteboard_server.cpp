#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <conio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using SocketType = SOCKET;
constexpr SocketType kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
using SocketType = int;
constexpr SocketType kInvalidSocket = -1;
#endif

#include <atomic>
#include <cctype>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int kBoardWidth = 60;
constexpr int kBoardHeight = 20;

void waitForExitMessage(const std::string& message) {
#ifdef _WIN32
    std::cout << message << "\nPress any key to close...";
    std::cout.flush();
    _getch();
#else
    std::cout << message << "\n";
#endif
}

void closeSocket(SocketType s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

bool initSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void cleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

struct RoomState {
    std::vector<std::string> board;
    std::vector<SocketType> clients;
    bool hasPassword = false;
    std::string password;
    RoomState() : board(kBoardHeight, std::string(kBoardWidth, '.')) {}
};

std::mutex gRoomsMutex;
std::map<std::string, RoomState> gRooms;

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

std::string normalizeRoom(const std::string& room) {
    std::string out = trim(room);
    if (out.empty()) out = "main";
    if (out.size() > 64) out.resize(64);
    return out;
}

bool sendAll(SocketType sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        int n = send(sock, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
        ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
#endif
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool sendLine(SocketType sock, const std::string& line) { return sendAll(sock, line + "\n"); }

bool readLine(SocketType sock, std::string& out) {
    out.clear();
    char ch;
    while (true) {
#ifdef _WIN32
        int n = recv(sock, &ch, 1, 0);
#else
        ssize_t n = recv(sock, &ch, 1, 0);
#endif
        if (n <= 0) return false;
        if (ch == '\n') return true;
        if (ch != '\r') out.push_back(ch);
        if (out.size() > 8192) return false;
    }
}

std::string boardToWire(const std::vector<std::string>& board) {
    std::ostringstream out;
    for (size_t i = 0; i < board.size(); ++i) {
        if (i) out << '|';
        out << board[i];
    }
    return out.str();
}

std::vector<std::string> boardFromWire(const std::string& payload) {
    std::vector<std::string> board;
    std::stringstream ss(payload);
    std::string row;
    while (std::getline(ss, row, '|')) {
        if (row.size() < static_cast<size_t>(kBoardWidth)) row += std::string(kBoardWidth - row.size(), '.');
        if (row.size() > static_cast<size_t>(kBoardWidth)) row.resize(kBoardWidth);
        board.push_back(row);
    }
    while (board.size() < static_cast<size_t>(kBoardHeight)) board.push_back(std::string(kBoardWidth, '.'));
    if (board.size() > static_cast<size_t>(kBoardHeight)) board.resize(kBoardHeight);
    return board;
}

void removeClientLocked(SocketType sock) {
    for (auto& [_, room] : gRooms) {
        std::vector<SocketType> keep;
        for (SocketType c : room.clients) if (c != sock) keep.push_back(c);
        room.clients.swap(keep);
    }
}

void broadcastLocked(const std::string& room, const std::string& msg) {
    auto& clients = gRooms[room].clients;
    std::vector<SocketType> alive;
    for (SocketType c : clients) {
        if (sendLine(c, msg)) alive.push_back(c);
        else closeSocket(c);
    }
    clients.swap(alive);
}

bool attachLocked(SocketType sock, std::string& currentRoom, const std::string& targetRoom, const std::string& password,
                  bool create, std::string& err) {
    const std::string room = normalizeRoom(targetRoom);
    if (create && gRooms.count(room)) {
        err = "room already exists";
        return false;
    }
    if (!gRooms.count(room)) {
        RoomState rs;
        if (!password.empty()) { rs.hasPassword = true; rs.password = password; }
        gRooms[room] = rs;
    }

    auto& r = gRooms[room];
    if (r.hasPassword && r.password != password) {
        err = "wrong password";
        return false;
    }

    removeClientLocked(sock);
    currentRoom = room;
    r.clients.push_back(sock);
    sendLine(sock, "ROOM " + room + (r.hasPassword ? " locked" : " open"));
    sendLine(sock, "STATE " + boardToWire(r.board));
    return true;
}

void handleClient(SocketType sock) {
    std::string room = "main";
    if (!sendLine(sock, "HELLO 60 20")) {
        closeSocket(sock);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(gRoomsMutex);
        if (!gRooms.count("main")) gRooms["main"] = RoomState{};
        gRooms["main"].clients.push_back(sock);
        sendLine(sock, "ROOM main open");
        sendLine(sock, "STATE " + boardToWire(gRooms["main"].board));
    }

    std::string line;
    while (readLine(sock, line)) {
        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "JOIN" || cmd == "CREATE") {
            std::string target, pw;
            ss >> target >> pw;
            if (target.empty()) { sendLine(sock, "ERROR missing room name"); continue; }
            std::lock_guard<std::mutex> lock(gRoomsMutex);
            std::string err;
            if (!attachLocked(sock, room, target, pw, cmd == "CREATE", err)) sendLine(sock, "ERROR " + err);
            continue;
        }
        if (cmd == "DRAW") {
            int x = -1, y = -1; char mark = '#';
            ss >> x >> y >> mark;
            if (!ss || x < 0 || y < 0 || x >= kBoardWidth || y >= kBoardHeight) { sendLine(sock, "ERROR invalid DRAW"); continue; }
            std::lock_guard<std::mutex> lock(gRoomsMutex);
            gRooms[room].board[y][x] = mark;
            broadcastLocked(room, "EVENT DRAW " + std::to_string(x) + " " + std::to_string(y) + " " + std::string(1, mark));
            continue;
        }
        if (cmd == "CLEAR") {
            std::lock_guard<std::mutex> lock(gRoomsMutex);
            gRooms[room].board.assign(kBoardHeight, std::string(kBoardWidth, '.'));
            broadcastLocked(room, "EVENT CLEAR");
            continue;
        }
        if (cmd == "QUIT") break;
    }

    {
        std::lock_guard<std::mutex> lock(gRoomsMutex);
        removeClientLocked(sock);
    }
    closeSocket(sock);
}

int runServer(int port) {
    SocketType serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock == kInvalidSocket) return 1;

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(serverSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return 1;
    if (listen(serverSock, 64) < 0) return 1;

    std::cout << "Server running on port " << port << "\n";
    while (true) {
        sockaddr_in caddr{};
#ifdef _WIN32
        int len = sizeof(caddr);
#else
        socklen_t len = sizeof(caddr);
#endif
        SocketType c = accept(serverSock, reinterpret_cast<sockaddr*>(&caddr), &len);
        if (c == kInvalidSocket) continue;
        std::thread(handleClient, c).detach();
    }
}

struct UiState {
    std::vector<std::string> board = std::vector<std::string>(kBoardHeight, std::string(kBoardWidth, '.'));
    std::string room = "main";
    std::string mode = "open";
    std::string info = "connected";
    int x = 0;
    int y = 0;
    char pen = '#';
};

#ifndef _WIN32
struct RawMode {
    termios old{};
    bool enabled = false;
    void enable() {
        if (enabled) return;
        if (tcgetattr(STDIN_FILENO, &old) == 0) {
            termios raw = old;
            raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
            enabled = true;
        }
    }
    void disable() {
        if (enabled) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old);
            enabled = false;
        }
    }
    ~RawMode() { disable(); }
};
int readKey() {
    char c = 0;
    if (::read(STDIN_FILENO, &c, 1) <= 0) return -1;
    return static_cast<unsigned char>(c);
}
#else
struct RawMode { void enable() {} void disable() {} };
int readKey() { return _getch(); }
#endif

void render(const UiState& ui) {
    std::cout << "\x1b[2J\x1b[H";
    std::cout << "Shared Whiteboard UI (Windows-ready) | room=" << ui.room << " (" << ui.mode << ")\n";
    std::cout << "WASD move SPACE draw X clear P pen J join N create Q quit\n";
    std::cout << "Status: " << ui.info << "\n";
    for (int y = 0; y < kBoardHeight; ++y) {
        for (int x = 0; x < kBoardWidth; ++x) {
            if (x == ui.x && y == ui.y) std::cout << '[' << ui.board[y][x] << ']';
            else std::cout << ' ' << ui.board[y][x] << ' ';
        }
        std::cout << "\n";
    }
}

std::string prompt(const std::string& label, RawMode& raw) {
    raw.disable();
    std::cout << "\n" << label;
    std::string s;
    std::getline(std::cin, s);
    raw.enable();
    return trim(s);
}

int runClient(const std::string& host, int port, const std::string& room, const std::string& password, bool createRoom) {
    SocketType sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        waitForExitMessage("Failed to create socket.");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        closeSocket(sock);
        waitForExitMessage("Invalid host IP address.");
        return 1;
    }
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closeSocket(sock);
        waitForExitMessage("Could not connect. Start server first: SharedWhiteboard.exe --server --port 5050");
        return 1;
    }

    UiState ui;
    std::mutex mu;
    std::atomic<bool> live(true);

    std::thread reader([&]() {
        std::string line;
        while (live.load() && readLine(sock, line)) {
            std::stringstream ss(line);
            std::string head;
            ss >> head;
            std::lock_guard<std::mutex> lock(mu);
            if (head == "ROOM") {
                ss >> ui.room >> ui.mode;
                ui.info = "joined room";
            } else if (head == "STATE") {
                std::string payload;
                std::getline(ss, payload);
                ui.board = boardFromWire(trim(payload));
                ui.info = "state synced";
            } else if (head == "EVENT") {
                std::string kind;
                ss >> kind;
                if (kind == "DRAW") {
                    int x, y; char mark;
                    ss >> x >> y >> mark;
                    if (x >= 0 && x < kBoardWidth && y >= 0 && y < kBoardHeight) ui.board[y][x] = mark;
                    ui.info = "draw";
                } else if (kind == "CLEAR") {
                    ui.board.assign(kBoardHeight, std::string(kBoardWidth, '.'));
                    ui.info = "cleared";
                }
            } else if (head == "ERROR") {
                std::string m;
                std::getline(ss, m);
                ui.info = "error: " + trim(m);
            }
        }
        live.store(false);
    });

    std::string boot = (createRoom ? "CREATE " : "JOIN ") + normalizeRoom(room);
    if (!password.empty()) boot += " " + password;
    sendLine(sock, boot);

    RawMode raw;
    raw.enable();
    while (live.load()) {
        {
            std::lock_guard<std::mutex> lock(mu);
            render(ui);
        }
        int key = readKey();
        if (key < 0) break;
        char c = static_cast<char>(std::tolower(key));
        std::lock_guard<std::mutex> lock(mu);
        if (c == 'q') { sendLine(sock, "QUIT"); break; }
        if (c == 'w' && ui.y > 0) ui.y--;
        else if (c == 's' && ui.y < kBoardHeight - 1) ui.y++;
        else if (c == 'a' && ui.x > 0) ui.x--;
        else if (c == 'd' && ui.x < kBoardWidth - 1) ui.x++;
        else if (c == ' ') sendLine(sock, "DRAW " + std::to_string(ui.x) + " " + std::to_string(ui.y) + " " + std::string(1, ui.pen));
        else if (c == 'x') sendLine(sock, "CLEAR");
        else if (c == 'p') ui.pen = (ui.pen == '#') ? '@' : '#';
        else if (c == 'j' || c == 'n') {
            std::string r = prompt(c == 'j' ? "Join room: " : "Create room: ", raw);
            std::string pw = prompt("Password (empty for open): ", raw);
            if (!r.empty()) {
                std::string cmd = (c == 'j' ? "JOIN " : "CREATE ") + normalizeRoom(r);
                if (!pw.empty()) cmd += " " + pw;
                sendLine(sock, cmd);
            }
        }
    }

    raw.disable();
    live.store(false);
#ifdef _WIN32
    shutdown(sock, SD_BOTH);
#else
    shutdown(sock, SHUT_RDWR);
#endif
    closeSocket(sock);
    if (reader.joinable()) reader.join();
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (!initSockets()) {
        std::cerr << "Socket init failed\n";
        return 1;
    }

    bool serverMode = false;
    bool createRoom = false;
    std::string host = "127.0.0.1";
    int port = 5050;
    std::string room = "main";
    std::string password;

#ifdef _WIN32
    if (argc == 1) {
        std::cout << "SharedWhiteboard Launcher\n1) Start Server\n2) Start Client UI\nChoose (1/2): ";
        int choice = _getch();
        std::cout << static_cast<char>(choice) << "\n";
        if (choice == '1') {
            serverMode = true;
            std::cout << "Port (default 5050): ";
            std::string p;
            std::getline(std::cin, p);
            p = trim(p);
            if (!p.empty()) port = std::stoi(p);
        } else {
            std::cout << "Host (default 127.0.0.1): ";
            std::getline(std::cin, host);
            host = trim(host);
            if (host.empty()) host = "127.0.0.1";
            std::cout << "Port (default 5050): ";
            std::string p;
            std::getline(std::cin, p);
            p = trim(p);
            if (!p.empty()) port = std::stoi(p);
            std::cout << "Room (default main): ";
            std::getline(std::cin, room);
            room = trim(room);
            if (room.empty()) room = "main";
            std::cout << "Create room? (y/N): ";
            std::string c;
            std::getline(std::cin, c);
            createRoom = !c.empty() && (c[0] == 'y' || c[0] == 'Y');
            std::cout << "Password (optional): ";
            std::getline(std::cin, password);
            password = trim(password);
        }
    }
#endif

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") serverMode = true;
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--room" && i + 1 < argc) room = argv[++i];
        else if (arg == "--password" && i + 1 < argc) password = argv[++i];
        else if (arg == "--create-room") createRoom = true;
    }

    const int code = serverMode ? runServer(port) : runClient(host, port, room, password, createRoom);
    cleanupSockets();
    return code;
}
