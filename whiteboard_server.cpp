#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
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

struct RoomState {
    std::vector<std::string> board;
    std::vector<int> clients;

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

bool sendAll(int sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool sendLine(int sock, const std::string& line) {
    return sendAll(sock, line + "\n");
}

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

std::string normalizeRoom(const std::string& room) {
    std::string out = trim(room);
    if (out.empty()) out = "main";
    if (out.size() > 64) out.resize(64);
    return out;
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
    alive.reserve(room.clients.size());
    for (int c : room.clients) {
        if (sendLine(c, line)) {
            alive.push_back(c);
        } else {
            close(c);
        }
    }
    room.clients.swap(alive);
}

void handleServerClient(int clientSock) {
    std::string roomName = "main";

    if (!sendLine(clientSock, "HELLO " + std::to_string(kBoardWidth) + " " + std::to_string(kBoardHeight))) {
        close(clientSock);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(gRoomsMutex);
        roomName = "main";
        gRooms[roomName].clients.push_back(clientSock);
        sendLine(clientSock, "STATE " + boardToWire(gRooms[roomName].board));
    }

    std::string line;
    while (readLine(clientSock, line)) {
        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "JOIN") {
            std::string newRoom;
            ss >> newRoom;
            newRoom = normalizeRoom(newRoom);

            std::lock_guard<std::mutex> lock(gRoomsMutex);
            auto& oldClients = gRooms[roomName].clients;
            std::vector<int> keep;
            for (int c : oldClients) {
                if (c != clientSock) keep.push_back(c);
            }
            oldClients.swap(keep);

            roomName = newRoom;
            gRooms[roomName].clients.push_back(clientSock);
            sendLine(clientSock, "STATE " + boardToWire(gRooms[roomName].board));
            continue;
        }

        if (cmd == "DRAW") {
            int x = -1;
            int y = -1;
            char mark = '#';
            ss >> x >> y >> mark;
            if (!ss || x < 0 || y < 0 || x >= kBoardWidth || y >= kBoardHeight) {
                sendLine(clientSock, "ERROR invalid DRAW. usage: DRAW x y #");
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

        if (cmd == "QUIT") {
            break;
        }

        sendLine(clientSock, "ERROR unknown command");
    }

    cleanupClientFromRooms(clientSock);
    close(clientSock);
}

int runServer(int port) {
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

    std::cout << "SharedWhiteboard server mode running on port " << port << "\n";
    std::cout << "Clients connect using this same EXE in client mode.\n";

    while (gRunning.load()) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSock = accept(serverSock, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientSock < 0) {
            if (gRunning.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        std::thread(handleServerClient, clientSock).detach();
    }

    close(serverSock);
    return 0;
}

void printBoard(const std::vector<std::string>& board) {
    std::cout << "\n";
    for (const auto& row : board) {
        std::cout << row << "\n";
    }
    std::cout << std::flush;
}

int runClient(const std::string& host, int port, const std::string& room) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid host IP\n";
        close(sock);
        return 1;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to " << host << ':' << port << "\n";
        close(sock);
        return 1;
    }

    std::mutex boardMutex;
    std::vector<std::string> board(kBoardHeight, std::string(kBoardWidth, '.'));
    std::atomic<bool> connected(true);

    std::thread reader([&]() {
        std::string line;
        while (connected.load() && readLine(sock, line)) {
            std::stringstream ss(line);
            std::string head;
            ss >> head;

            if (head == "HELLO") {
                int w = 0, h = 0;
                ss >> w >> h;
                std::cout << "Connected. Board size " << w << "x" << h << "\n";
                continue;
            }

            if (head == "STATE") {
                std::string payload;
                std::getline(ss, payload);
                payload = trim(payload);
                std::lock_guard<std::mutex> lock(boardMutex);
                board = boardFromWire(payload);
                std::cout << "[state synced]\n";
                printBoard(board);
                continue;
            }

            if (head == "EVENT") {
                std::string type;
                ss >> type;
                if (type == "DRAW") {
                    int x = -1, y = -1;
                    char mark = '#';
                    ss >> x >> y >> mark;
                    if (x >= 0 && x < kBoardWidth && y >= 0 && y < kBoardHeight) {
                        std::lock_guard<std::mutex> lock(boardMutex);
                        board[y][x] = mark;
                        std::cout << "[draw " << x << ',' << y << "]\n";
                    }
                } else if (type == "CLEAR") {
                    std::lock_guard<std::mutex> lock(boardMutex);
                    board.assign(kBoardHeight, std::string(kBoardWidth, '.'));
                    std::cout << "[room cleared]\n";
                }
                continue;
            }

            if (head == "ERROR") {
                std::string message;
                std::getline(ss, message);
                std::cout << "Server error:" << message << "\n";
            }
        }

        connected.store(false);
    });

    sendLine(sock, "JOIN " + normalizeRoom(room));

    std::cout << "Commands: draw x y [char], clear, show, room <name>, state, quit\n";
    std::string input;
    while (connected.load() && std::getline(std::cin, input)) {
        const std::string cmdLine = trim(input);
        if (cmdLine.empty()) continue;

        std::stringstream ss(cmdLine);
        std::string cmd;
        ss >> cmd;

        if (cmd == "draw") {
            int x = -1, y = -1;
            char mark = '#';
            ss >> x >> y;
            if (ss >> mark) {}
            if (x < 0 || y < 0 || x >= kBoardWidth || y >= kBoardHeight) {
                std::cout << "Invalid coordinates\n";
                continue;
            }
            sendLine(sock, "DRAW " + std::to_string(x) + " " + std::to_string(y) + " " + std::string(1, mark));
            continue;
        }

        if (cmd == "clear") {
            sendLine(sock, "CLEAR");
            continue;
        }

        if (cmd == "show") {
            std::lock_guard<std::mutex> lock(boardMutex);
            printBoard(board);
            continue;
        }

        if (cmd == "room") {
            std::string newRoom;
            ss >> newRoom;
            if (newRoom.empty()) {
                std::cout << "usage: room <name>\n";
                continue;
            }
            sendLine(sock, "JOIN " + normalizeRoom(newRoom));
            continue;
        }

        if (cmd == "state") {
            sendLine(sock, "STATE");
            continue;
        }

        if (cmd == "quit") {
            sendLine(sock, "QUIT");
            break;
        }

        std::cout << "Unknown command\n";
    }

    connected.store(false);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    if (reader.joinable()) reader.join();
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    bool serverMode = false;
    std::string host = "127.0.0.1";
    int port = 5050;
    std::string room = "main";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--server") {
            serverMode = true;
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--room" && i + 1 < argc) {
            room = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage:\n"
                      << "  SharedWhiteboard.exe --server --port 5050\n"
                      << "  SharedWhiteboard.exe --host 192.168.1.10 --port 5050 --room main\n";
            return 0;
        }
    }

    if (serverMode) return runServer(port);
    return runClient(host, port, room);
}
