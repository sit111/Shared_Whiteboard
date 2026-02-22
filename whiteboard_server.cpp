#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")

#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int BOARD_W = 64;
constexpr int BOARD_H = 36;
constexpr int TOOLBAR_H = 92;
constexpr int CELL = 16;
constexpr UINT WM_NET_LINE = WM_APP + 1;

enum : int {
    ID_HOST = 101,
    ID_PORT,
    ID_ROOM,
    ID_PASS,
    ID_JOIN,
    ID_CREATE,
    ID_CLEAR,
    ID_PEN,
    ID_STATUS,
    ID_ACTIVE_ROOM
};

struct RoomState {
    std::vector<std::string> board = std::vector<std::string>(BOARD_H, std::string(BOARD_W, '.'));
    std::vector<SOCKET> clients;
    bool hasPassword = false;
    std::string password;
};

std::map<std::string, RoomState> g_rooms;
std::mutex g_roomsMutex;

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool sendAll(SOCKET s, const std::string& d) {
    size_t off = 0;
    while (off < d.size()) {
        int n = send(s, d.data() + off, (int)(d.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

bool sendLine(SOCKET s, const std::string& l) { return sendAll(s, l + "\n"); }

bool readLine(SOCKET s, std::string& out) {
    out.clear();
    char ch;
    while (true) {
        int n = recv(s, &ch, 1, 0);
        if (n <= 0) return false;
        if (ch == '\n') return true;
        if (ch != '\r') out.push_back(ch);
        if (out.size() > 8192) return false;
    }
}

std::string boardToWire(const std::vector<std::string>& b) {
    std::ostringstream os;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i) os << '|';
        os << b[i];
    }
    return os.str();
}

std::vector<std::string> boardFromWire(const std::string& payload) {
    std::vector<std::string> b;
    std::stringstream ss(payload);
    std::string row;
    while (std::getline(ss, row, '|')) {
        if ((int)row.size() < BOARD_W) row += std::string(BOARD_W - row.size(), '.');
        if ((int)row.size() > BOARD_W) row.resize(BOARD_W);
        b.push_back(row);
    }
    while ((int)b.size() < BOARD_H) b.push_back(std::string(BOARD_W, '.'));
    if ((int)b.size() > BOARD_H) b.resize(BOARD_H);
    return b;
}

void removeClientLocked(SOCKET s) {
    for (auto& [_, room] : g_rooms) {
        std::vector<SOCKET> keep;
        for (SOCKET c : room.clients) if (c != s) keep.push_back(c);
        room.clients.swap(keep);
    }
}

void broadcastLocked(const std::string& roomName, const std::string& msg) {
    auto& clients = g_rooms[roomName].clients;
    std::vector<SOCKET> alive;
    for (SOCKET c : clients) {
        if (sendLine(c, msg)) alive.push_back(c);
        else closesocket(c);
    }
    clients.swap(alive);
}

bool attachLocked(SOCKET s, std::string& currentRoom, const std::string& targetRoom, const std::string& pw, bool create, std::string& err) {
    std::string room = trim(targetRoom);
    if (room.empty()) room = "main";
    if (create && g_rooms.count(room)) { err = "room already exists"; return false; }
    if (!g_rooms.count(room)) {
        RoomState rs;
        if (!pw.empty()) { rs.hasPassword = true; rs.password = pw; }
        g_rooms[room] = rs;
    }
    auto& r = g_rooms[room];
    if (r.hasPassword && r.password != pw) { err = "wrong password"; return false; }
    removeClientLocked(s);
    currentRoom = room;
    r.clients.push_back(s);
    sendLine(s, "ROOM " + room + (r.hasPassword ? " locked" : " open"));
    sendLine(s, "STATE " + boardToWire(r.board));
    return true;
}

void serverClientThread(SOCKET s) {
    std::string room = "main";
    sendLine(s, "HELLO");
    {
        std::lock_guard<std::mutex> lk(g_roomsMutex);
        if (!g_rooms.count("main")) g_rooms["main"] = RoomState{};
        g_rooms["main"].clients.push_back(s);
        sendLine(s, "ROOM main open");
        sendLine(s, "STATE " + boardToWire(g_rooms["main"].board));
    }

    std::string line;
    while (readLine(s, line)) {
        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd == "JOIN" || cmd == "CREATE") {
            std::string r, p; ss >> r >> p;
            std::lock_guard<std::mutex> lk(g_roomsMutex);
            std::string err;
            if (!attachLocked(s, room, r, p, cmd == "CREATE", err)) sendLine(s, "ERROR " + err);
            continue;
        }
        if (cmd == "DRAW") {
            int x=-1, y=-1; char m='#'; ss >> x >> y >> m;
            if (x < 0 || x >= BOARD_W || y < 0 || y >= BOARD_H) continue;
            std::lock_guard<std::mutex> lk(g_roomsMutex);
            g_rooms[room].board[y][x] = m;
            broadcastLocked(room, "EVENT DRAW " + std::to_string(x) + " " + std::to_string(y) + " " + std::string(1,m));
            continue;
        }
        if (cmd == "CLEAR") {
            std::lock_guard<std::mutex> lk(g_roomsMutex);
            g_rooms[room].board.assign(BOARD_H, std::string(BOARD_W, '.'));
            broadcastLocked(room, "EVENT CLEAR");
            continue;
        }
        if (cmd == "QUIT") break;
    }

    {
        std::lock_guard<std::mutex> lk(g_roomsMutex);
        removeClientLocked(s);
    }
    closesocket(s);
}

int runServer(int port) {
    SOCKET ss = socket(AF_INET, SOCK_STREAM, 0);
    if (ss == INVALID_SOCKET) return 1;
    int opt = 1;
    setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons((u_short)port);
    if (bind(ss,(sockaddr*)&a,sizeof(a))<0) return 1;
    if (listen(ss,64)<0) return 1;
    MessageBoxA(nullptr, "Server started. Keep this window open while people connect.", "SharedWhiteboard Server", MB_OK | MB_ICONINFORMATION);
    while (true) {
        sockaddr_in ca{}; int len=sizeof(ca);
        SOCKET c=accept(ss,(sockaddr*)&ca,&len);
        if (c==INVALID_SOCKET) continue;
        std::thread(serverClientThread,c).detach();
    }
}

struct ClientApp {
    HWND hwnd{};
    HWND eHost{}, ePort{}, eRoom{}, ePass{}, lblStatus{}, lblRoom{}, btnPen{};
    SOCKET sock = INVALID_SOCKET;
    std::thread reader;
    std::vector<std::string> board = std::vector<std::string>(BOARD_H, std::string(BOARD_W, '.'));
    std::string activeRoom = "none";
    std::string roomMode = "open";
    char pen = '#';
    bool connected = false;
} g_app;

void setStatus(const std::string& s) {
    SetWindowTextA(g_app.lblStatus, s.c_str());
}

void setActiveRoom(const std::string& room, const std::string& mode) {
    g_app.activeRoom = room;
    g_app.roomMode = mode;
    std::string line = "Active Room: " + room + " (" + mode + ")";
    SetWindowTextA(g_app.lblRoom, line.c_str());
}

std::string getText(HWND h) {
    char b[256]{};
    GetWindowTextA(h, b, sizeof(b));
    return std::string(b);
}

void closeClientConnection() {
    if (g_app.connected) {
        g_app.connected = false;
        shutdown(g_app.sock, SD_BOTH);
        closesocket(g_app.sock);
    }
    if (g_app.reader.joinable()) g_app.reader.join();
    g_app.sock = INVALID_SOCKET;
}

bool clientConnectAndRoom(bool createMode) {
    closeClientConnection();

    const std::string host = trim(getText(g_app.eHost)).empty() ? "127.0.0.1" : trim(getText(g_app.eHost));
    int port = atoi(getText(g_app.ePort).c_str()); if (port <= 0) port = 5050;
    const std::string room = trim(getText(g_app.eRoom)).empty() ? "main" : trim(getText(g_app.eRoom));
    const std::string pass = trim(getText(g_app.ePass));

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { setStatus("Socket create failed"); return false; }

    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) <= 0) {
        closesocket(s); setStatus("Invalid host"); return false;
    }
    if (connect(s, (sockaddr*)&a, sizeof(a)) < 0) {
        closesocket(s); setStatus("Cannot connect. Start server first."); return false;
    }

    g_app.sock = s;
    g_app.connected = true;
    setStatus("Connected. Joining room...");

    g_app.reader = std::thread([] {
        std::string line;
        while (g_app.connected && readLine(g_app.sock, line)) {
            PostMessage(g_app.hwnd, WM_NET_LINE, 0, (LPARAM)new std::string(line));
        }
        if (g_app.connected) {
            PostMessage(g_app.hwnd, WM_NET_LINE, 0, (LPARAM)new std::string("ERROR disconnected"));
        }
    });

    std::string cmd = std::string(createMode ? "CREATE " : "JOIN ") + room;
    if (!pass.empty()) cmd += " " + pass;
    sendLine(g_app.sock, cmd);
    return true;
}

void drawBoard(HDC hdc, RECT clientRect) {
    RECT boardRect{8, TOOLBAR_H, 8 + BOARD_W * CELL, TOOLBAR_H + BOARD_H * CELL};
    HBRUSH bg = CreateSolidBrush(RGB(250, 251, 255));
    FillRect(hdc, &boardRect, bg);
    DeleteObject(bg);

    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(220, 226, 236));
    HGDIOBJ oldPen = SelectObject(hdc, gridPen);

    for (int x = 0; x <= BOARD_W; ++x) {
        MoveToEx(hdc, boardRect.left + x * CELL, boardRect.top, nullptr);
        LineTo(hdc, boardRect.left + x * CELL, boardRect.bottom);
    }
    for (int y = 0; y <= BOARD_H; ++y) {
        MoveToEx(hdc, boardRect.left, boardRect.top + y * CELL, nullptr);
        LineTo(hdc, boardRect.right, boardRect.top + y * CELL);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(35, 40, 50));
    for (int y = 0; y < BOARD_H; ++y) {
        for (int x = 0; x < BOARD_W; ++x) {
            char c = g_app.board[y][x];
            if (c == '.') continue;
            char t[2]{c, 0};
            TextOutA(hdc, boardRect.left + x * CELL + 4, boardRect.top + y * CELL + 1, t, 1);
        }
    }

    RECT tip{boardRect.left, boardRect.bottom + 8, clientRect.right - 10, boardRect.bottom + 30};
    DrawTextA(hdc, "Draw with mouse drag. Press P to switch pen (#/@).", -1, &tip, DT_LEFT | DT_SINGLELINE);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            g_app.hwnd = h;
            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            auto mk = [&](const char* cls, const char* txt, DWORD style, int x, int y, int w, int h, int id=0) {
                HWND hw = CreateWindowA(cls, txt, style, x, y, w, h, g_app.hwnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessage(hw, WM_SETFONT, (WPARAM)font, TRUE);
                return hw;
            };

            mk("STATIC", "Host", WS_CHILD|WS_VISIBLE, 10,10,35,20);
            g_app.eHost = mk("EDIT", "127.0.0.1", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 50,8,120,22, ID_HOST);
            mk("STATIC", "Port", WS_CHILD|WS_VISIBLE, 180,10,30,20);
            g_app.ePort = mk("EDIT", "5050", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 214,8,65,22, ID_PORT);
            mk("STATIC", "Room", WS_CHILD|WS_VISIBLE, 290,10,35,20);
            g_app.eRoom = mk("EDIT", "main", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 328,8,110,22, ID_ROOM);
            mk("STATIC", "Password", WS_CHILD|WS_VISIBLE, 446,10,58,20);
            g_app.ePass = mk("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL, 507,8,100,22, ID_PASS);

            mk("BUTTON", "Join Room", WS_CHILD|WS_VISIBLE, 620,8,90,24, ID_JOIN);
            mk("BUTTON", "Create Room", WS_CHILD|WS_VISIBLE, 715,8,98,24, ID_CREATE);
            mk("BUTTON", "Clear Room", WS_CHILD|WS_VISIBLE, 620,38,90,24, ID_CLEAR);
            g_app.btnPen = mk("BUTTON", "Pen: #", WS_CHILD|WS_VISIBLE, 715,38,98,24, ID_PEN);

            g_app.lblStatus = mk("STATIC", "Status: Not connected", WS_CHILD|WS_VISIBLE, 10,40,600,20, ID_STATUS);
            g_app.lblRoom = mk("STATIC", "Active Room: none", WS_CHILD|WS_VISIBLE, 10,62,600,20, ID_ACTIVE_ROOM);

            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(w)) {
                case ID_JOIN: clientConnectAndRoom(false); break;
                case ID_CREATE: clientConnectAndRoom(true); break;
                case ID_CLEAR: if (g_app.connected) sendLine(g_app.sock, "CLEAR"); break;
                case ID_PEN:
                    g_app.pen = (g_app.pen == '#') ? '@' : '#';
                    SetWindowTextA(g_app.btnPen, (g_app.pen == '#') ? "Pen: #" : "Pen: @");
                    break;
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE: {
            if (m == WM_MOUSEMOVE && !(w & MK_LBUTTON)) return 0;
            int px = GET_X_LPARAM(l) - 8;
            int py = GET_Y_LPARAM(l) - TOOLBAR_H;
            int x = px / CELL;
            int y = py / CELL;
            if (x >= 0 && x < BOARD_W && y >= 0 && y < BOARD_H && g_app.connected) {
                sendLine(g_app.sock, "DRAW " + std::to_string(x) + " " + std::to_string(y) + " " + std::string(1, g_app.pen));
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (w == 'P') {
                g_app.pen = (g_app.pen == '#') ? '@' : '#';
                SetWindowTextA(g_app.btnPen, (g_app.pen == '#') ? "Pen: #" : "Pen: @");
            }
            return 0;
        case WM_NET_LINE: {
            std::unique_ptr<std::string> line((std::string*)l);
            std::stringstream ss(*line);
            std::string head;
            ss >> head;
            if (head == "ROOM") {
                std::string room, mode; ss >> room >> mode;
                setActiveRoom(room, mode);
                setStatus("Status: Connected");
            } else if (head == "STATE") {
                std::string payload; std::getline(ss, payload);
                g_app.board = boardFromWire(trim(payload));
                InvalidateRect(h, nullptr, FALSE);
            } else if (head == "EVENT") {
                std::string kind; ss >> kind;
                if (kind == "DRAW") {
                    int x, y; char c; ss >> x >> y >> c;
                    if (x>=0 && x<BOARD_W && y>=0 && y<BOARD_H) g_app.board[y][x] = c;
                } else if (kind == "CLEAR") {
                    g_app.board.assign(BOARD_H, std::string(BOARD_W, '.'));
                }
                InvalidateRect(h, nullptr, FALSE);
            } else if (head == "ERROR") {
                std::string msg; std::getline(ss, msg);
                setStatus("Status: " + trim(msg));
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);

            HBRUSH top = CreateSolidBrush(RGB(240, 244, 252));
            RECT bar{0,0,rc.right,TOOLBAR_H};
            FillRect(hdc, &bar, top);
            DeleteObject(top);

            drawBoard(hdc, rc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_DESTROY:
            closeClientConnection();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(h, m, w, l);
}

} // namespace

int APIENTRY WinMain(HINSTANCE hi, HINSTANCE, LPSTR cmd, int show) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 1;

    std::string args = cmd ? cmd : "";
    int port = 5050;
    auto ppos = args.find("--port ");
    if (ppos != std::string::npos) {
        port = atoi(args.c_str() + ppos + 7);
        if (port <= 0) port = 5050;
    }

    if (args.find("--server") != std::string::npos) {
        int code = runServer(port);
        WSACleanup();
        return code;
    }

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = "SharedWhiteboardWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA("SharedWhiteboardWindow", "Shared Whiteboard - Windows App",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        80, 60, 1040, 760,
        nullptr, nullptr, hi, nullptr);

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WSACleanup();
    return 0;
}

#else
#include <iostream>
int main() {
    std::cout << "This project is a Windows GUI app. Build and run on Windows.\n";
    return 0;
}
#endif
