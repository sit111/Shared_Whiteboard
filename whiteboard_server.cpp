#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <termios.h>
#else
#include <conio.h>
#endif

namespace {

constexpr int kBoardWidth = 60;
constexpr int kBoardHeight = 20;

struct RoomState {
    std::vector<std::string> board;
    std::vector<int> clients;
    bool hasPassword = false;
    std::string password;

    RoomState() : board(kBoardHeight, std::string(kBoardWidth, '.')) {}
};

std::mutex gRoomsMutex;
std::map<std::string, RoomState> gRooms;
std::atomic<bool> gRunning(true);

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

bool sendAll(int sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool sendLine(int sock, const std::string& line) { return sendAll(sock, line + "\n"); }

bool readLine(int sock, std::string& lineOut) {
    lineOut.clear();
    char ch;
    while (true) {
        const ssize_t n = recv(sock, &ch, 1, 0);
        if (n <= 0) return false;
        if (ch == '\n') return true;
        if (ch != '\r') lineOut.push_back(ch);
        if (lineOut.size() > 8192) return false;
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

void cleanupClientFromRooms(int clientSock) {
    std::lock_guard<std::mutex> lock(gRoomsMutex);
    for (auto& [_, room] : gRooms) {
        std::vector<int> keep;
        for (int c : room.clients) {
            if (c != clientSock) keep.push_back(c);
        }
        room.clients.swap(keep);
    }
}

void broadcastToRoomLocked(const std::string& roomName, const std::string& line) {
    auto& room = gRooms[roomName];
    std::vector<int> alive;
    for (int c : room.clients) {
        if (sendLine(c, line)) {
            alive.push_back(c);
        } else {
            close(c);
        }
    }
    room.clients.swap(alive);
}

bool attachClientToRoomLocked(int clientSock, std::string& activeRoom, const std::string& roomName, const std::string& providedPassword,
                              bool createMode, std::string& errorOut) {
    const std::string normalized = normalizeRoom(roomName);

    if (createMode) {
        if (gRooms.count(normalized)) {
            errorOut = "room already exists";
            return false;
        }
        RoomState room;
        if (!providedPassword.empty()) {
            room.hasPassword = true;
            room.password = providedPassword;
        }
        gRooms[normalized] = room;
    } else if (!gRooms.count(normalized)) {
        RoomState room;
        if (!providedPassword.empty()) {
            room.hasPassword = true;
            room.password = providedPassword;
        }
        gRooms[normalized] = room;
    }

    auto& target = gRooms[normalized];
    if (target.hasPassword && target.password != providedPassword) {
        errorOut = "wrong password";
        return false;
    }

    auto& oldClients = gRooms[activeRoom].clients;
    std::vector<int> keep;
    for (int c : oldClients) {
        if (c != clientSock) keep.push_back(c);
    }
    oldClients.swap(keep);

    activeRoom = normalized;
    target.clients.push_back(clientSock);
    sendLine(clientSock, std::string("ROOM ") + activeRoom + (target.hasPassword ? " locked" : " open"));
    sendLine(clientSock, "STATE " + boardToWire(target.board));
    return true;
}

void handleServerClient(int clientSock) {
    std::string roomName = "main";
    if (!sendLine(clientSock, "HELLO 60 20")) {
        close(clientSock);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(gRoomsMutex);
        if (!gRooms.count("main")) gRooms["main"] = RoomState{};
        gRooms["main"].clients.push_back(clientSock);
        sendLine(clientSock, "ROOM main open");
        sendLine(clientSock, "STATE " + boardToWire(gRooms["main"].board));
    }

    std::string line;
    while (readLine(clientSock, line)) {
        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "JOIN" || cmd == "CREATE") {
            std::string newRoom, password;
            ss >> newRoom >> password;
            if (newRoom.empty()) {
                sendLine(clientSock, std::string("ERROR usage: ") + (cmd == "CREATE" ? "CREATE room [password]" : "JOIN room [password]"));
                continue;
            }
            std::lock_guard<std::mutex> lock(gRoomsMutex);
            std::string error;
            if (!attachClientToRoomLocked(clientSock, roomName, newRoom, password, cmd == "CREATE", error)) {
                sendLine(clientSock, "ERROR " + error);
            }
            continue;
        }

        if (cmd == "DRAW") {
            int x = -1, y = -1;
            char mark = '#';
            ss >> x >> y >> mark;
            if (!ss || x < 0 || y < 0 || x >= kBoardWidth || y >= kBoardHeight) {
                sendLine(clientSock, "ERROR invalid DRAW");
                continue;
            }
            std::lock_guard<std::mutex> lock(gRoomsMutex);
            gRooms[roomName].board[y][x] = mark;
            broadcastToRoomLocked(roomName, "EVENT DRAW " + std::to_string(x) + " " + std::to_string(y) + " " + std::string(1, mark));
            continue;
        }

        if (cmd == "CLEAR") {
            std::lock_guard<std::mutex> lock(gRoomsMutex);
            gRooms[roomName].board.assign(kBoardHeight, std::string(kBoardWidth, '.'));
            broadcastToRoomLocked(roomName, "EVENT CLEAR");
            continue;
        }

        if (cmd == "STATE") {
            std::lock_guard<std::mutex> lock(gRoomsMutex);
            sendLine(clientSock, "STATE " + boardToWire(gRooms[roomName].board));
            continue;
        }

        if (cmd == "QUIT") break;
        sendLine(clientSock, "ERROR unknown command");
    }

    cleanupClientFromRooms(clientSock);
    close(clientSock);
}

int runServer(int port) {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) return 1;
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(serverSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return 1;
    if (listen(serverSock, 64) < 0) return 1;

    std::cout << "Server running on port " << port << "\n";
    while (gRunning.load()) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(serverSock, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientSock < 0) continue;
        std::thread(handleServerClient, clientSock).detach();
    }
    close(serverSock);
    return 0;
}

struct UiState {
    std::vector<std::string> board = std::vector<std::string>(kBoardHeight, std::string(kBoardWidth, '.'));
    std::string room = "main";
    std::string mode = "open";
    std::string info = "connected";
    int cursorX = 0;
    int cursorY = 0;
    char pen = '#';
};

#ifndef _WIN32
struct RawMode {
    termios old{};
    bool enabled = false;
    explicit RawMode(bool on = true) {
        if (!on) return;
        if (tcgetattr(STDIN_FILENO, &old) == 0) {
            termios raw = old;
            raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
            enabled = true;
        }
    }
    ~RawMode() {
        if (enabled) tcsetattr(STDIN_FILENO, TCSANOW, &old);
    }
};

int readKey() {
    char c = 0;
    if (::read(STDIN_FILENO, &c, 1) <= 0) return -1;
    return static_cast<unsigned char>(c);
}
#else
struct RawMode { explicit RawMode(bool = true) {} };
int readKey() { return _getch(); }
#endif

void renderUi(const UiState& ui) {
    std::cout << "\x1b[2J\x1b[H";
    std::cout << "Shared Whiteboard UI | room=" << ui.room << " (" << ui.mode << ") | pen=" << ui.pen << "\n";
    std::cout << "WASD move  SPACE draw  X clear  J join  N new room  P pen  Q quit\n";
    std::cout << "Status: " << ui.info << "\n";

    for (int y = 0; y < kBoardHeight; ++y) {
        for (int x = 0; x < kBoardWidth; ++x) {
            if (x == ui.cursorX && y == ui.cursorY) {
                std::cout << '[' << ui.board[y][x] << ']';
            } else {
                std::cout << ' ' << ui.board[y][x] << ' ';
            }
        }
        std::cout << "\n";
    }
    std::cout.flush();
}

std::string promptLine(const std::string& label) {
#ifndef _WIN32
    RawMode off(false);
#endif
    std::cout << "\n" << label;
    std::string s;
    std::getline(std::cin, s);
    return trim(s);
}

int runClient(const std::string& host, int port, const std::string& room, const std::string& password, bool createRoom) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) return 1;
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return 1;

    UiState ui;
    std::mutex uiMutex;
    std::atomic<bool> connected(true);

    std::thread reader([&]() {
        std::string line;
        while (connected.load() && readLine(sock, line)) {
            std::stringstream ss(line);
            std::string head;
            ss >> head;
            std::lock_guard<std::mutex> lock(uiMutex);
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
                    int x, y;
                    char mark;
                    ss >> x >> y >> mark;
                    if (x >= 0 && x < kBoardWidth && y >= 0 && y < kBoardHeight) ui.board[y][x] = mark;
                    ui.info = "draw event";
                } else if (kind == "CLEAR") {
                    ui.board.assign(kBoardHeight, std::string(kBoardWidth, '.'));
                    ui.info = "room cleared";
                }
            } else if (head == "ERROR") {
                std::string msg;
                std::getline(ss, msg);
                ui.info = "error: " + trim(msg);
            }
        }
        connected.store(false);
    });

    std::string boot = createRoom ? "CREATE " : "JOIN ";
    boot += normalizeRoom(room);
    if (!password.empty()) boot += " " + password;
    sendLine(sock, boot);

    RawMode raw(true);
    while (connected.load()) {
        {
            std::lock_guard<std::mutex> lock(uiMutex);
            renderUi(ui);
        }

        int key = readKey();
        if (key < 0) break;
        char c = static_cast<char>(std::tolower(key));

        std::lock_guard<std::mutex> lock(uiMutex);
        if (c == 'q') {
            sendLine(sock, "QUIT");
            break;
        } else if (c == 'w' && ui.cursorY > 0) {
            ui.cursorY--;
        } else if (c == 's' && ui.cursorY < kBoardHeight - 1) {
            ui.cursorY++;
        } else if (c == 'a' && ui.cursorX > 0) {
            ui.cursorX--;
        } else if (c == 'd' && ui.cursorX < kBoardWidth - 1) {
            ui.cursorX++;
        } else if (c == ' ') {
            sendLine(sock, "DRAW " + std::to_string(ui.cursorX) + " " + std::to_string(ui.cursorY) + " " + std::string(1, ui.pen));
        } else if (c == 'x') {
            sendLine(sock, "CLEAR");
        } else if (c == 'p') {
            ui.info = "pen changed";
            ui.pen = (ui.pen == '#') ? '@' : '#';
        } else if (c == 'j' || c == 'n') {
#ifndef _WIN32
            raw.~RawMode();
            new (&raw) RawMode(false);
#endif
            std::string target = promptLine(c == 'j' ? "Join room: " : "Create room: ");
            std::string pass = promptLine("Password (empty for open): ");
#ifndef _WIN32
            raw.~RawMode();
            new (&raw) RawMode(true);
#endif
            if (!target.empty()) {
                std::string cmd = (c == 'j' ? "JOIN " : "CREATE ") + normalizeRoom(target);
                if (!pass.empty()) cmd += " " + pass;
                sendLine(sock, cmd);
            }
        }
    }

    connected.store(false);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    if (reader.joinable()) reader.join();
    std::cout << "\nDisconnected.\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    bool serverMode = false;
    bool createRoom = false;
    std::string host = "127.0.0.1";
    int port = 5050;
    std::string room = "main";
    std::string password;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") serverMode = true;
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--room" && i + 1 < argc) room = argv[++i];
        else if (arg == "--password" && i + 1 < argc) password = argv[++i];
        else if (arg == "--create-room") createRoom = true;
        else if (arg == "--help") {
            std::cout << "Usage:\n"
                      << "  SharedWhiteboard.exe --server --port 5050\n"
                      << "  SharedWhiteboard.exe --host 192.168.1.10 --port 5050 --room team [--password secret] [--create-room]\n";
            return 0;
        }
    }

    if (serverMode) return runServer(port);
    return runClient(host, port, room, password, createRoom);
}
