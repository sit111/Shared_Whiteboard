#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")

#include <atomic>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <memory>

namespace {
constexpr int BOARD_W = 60;
constexpr int BOARD_H = 20;
constexpr int CELL = 14;
constexpr UINT WM_NET_LINE = WM_APP + 1;

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
    std::string room = trim(targetRoom); if (room.empty()) room = "main";
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
    sendLine(s, "HELLO 60 20");
    {
        std::lock_guard<std::mutex> lk(g_roomsMutex);
        if (!g_rooms.count("main")) g_rooms["main"] = RoomState{};
        g_rooms["main"].clients.push_back(s);
        sendLine(s, "ROOM main open");
        sendLine(s, "STATE " + boardToWire(g_rooms["main"].board));
    }
    std::string line;
    while (readLine(s, line)) {
        std::stringstream ss(line); std::string cmd; ss >> cmd;
        if (cmd == "JOIN" || cmd == "CREATE") {
            std::string r, p; ss >> r >> p;
            std::lock_guard<std::mutex> lk(g_roomsMutex);
            std::string err;
            if (!attachLocked(s, room, r, p, cmd == "CREATE", err)) sendLine(s, "ERROR " + err);
            continue;
        }
        if (cmd == "DRAW") {
            int x=-1,y=-1; char m='#'; ss>>x>>y>>m;
            if (x<0||x>=BOARD_W||y<0||y>=BOARD_H) continue;
            std::lock_guard<std::mutex> lk(g_roomsMutex);
            g_rooms[room].board[y][x]=m;
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
    MessageBoxA(nullptr, "Server is running on selected port. Keep this app open.", "SharedWhiteboard Server", MB_OK);
    while (true) {
        sockaddr_in ca{}; int len=sizeof(ca);
        SOCKET c=accept(ss,(sockaddr*)&ca,&len);
        if (c==INVALID_SOCKET) continue;
        std::thread(serverClientThread,c).detach();
    }
}

struct ClientApp {
    HWND hwnd{};
    HWND eHost{}, ePort{}, eRoom{}, ePass{}, lbl{};
    SOCKET sock = INVALID_SOCKET;
    std::thread reader;
    std::mutex mu;
    std::vector<std::string> board = std::vector<std::string>(BOARD_H, std::string(BOARD_W, '.'));
    std::string room = "main";
    char pen = '#';
    bool connected = false;
} g_app;

void setStatus(const std::string& s) {
    SetWindowTextA(g_app.lbl, s.c_str());
}

std::string getText(HWND h) {
    char buf[256]; GetWindowTextA(h, buf, sizeof(buf)); return std::string(buf);
}

bool clientConnectAndJoin(bool createMode) {
    if (g_app.connected) {
        closesocket(g_app.sock);
        if (g_app.reader.joinable()) g_app.reader.join();
        g_app.connected = false;
    }

    std::string host = trim(getText(g_app.eHost)); if (host.empty()) host = "127.0.0.1";
    int port = atoi(getText(g_app.ePort).c_str()); if (port <= 0) port = 5050;
    std::string room = trim(getText(g_app.eRoom)); if (room.empty()) room = "main";
    std::string pass = trim(getText(g_app.ePass));

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { setStatus("Socket create failed"); return false; }
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) <= 0) { closesocket(s); setStatus("Invalid host"); return false; }
    if (connect(s, (sockaddr*)&a, sizeof(a)) < 0) { closesocket(s); setStatus("Connect failed (start server first)"); return false; }

    g_app.sock = s;
    g_app.connected = true;
    g_app.reader = std::thread([] {
        std::string line;
        while (g_app.connected && readLine(g_app.sock, line)) {
            auto* heap = new std::string(line);
            PostMessage(g_app.hwnd, WM_NET_LINE, 0, (LPARAM)heap);
        }
        g_app.connected = false;
        PostMessage(g_app.hwnd, WM_NET_LINE, 0, (LPARAM)new std::string("ERROR disconnected"));
    });

    std::string cmd = std::string(createMode ? "CREATE " : "JOIN ") + room;
    if (!pass.empty()) cmd += " " + pass;
    sendLine(g_app.sock, cmd);
    return true;
}

void drawCell(HDC hdc, int x, int y, char ch, bool cursor) {
    RECT r{ x * CELL, y * CELL + 80, x * CELL + CELL, y * CELL + 80 + CELL };
    HBRUSH b = CreateSolidBrush(cursor ? RGB(210,230,255) : RGB(255,255,255));
    FillRect(hdc, &r, b); DeleteObject(b);
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    if (ch != '.') {
        char t[2]{ch,0};
        SetTextColor(hdc, RGB(30,30,30));
        SetBkMode(hdc, TRANSPARENT);
        TextOutA(hdc, r.left + 4, r.top + 1, t, 1);
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int cx = 0, cy = 0;
    switch (m) {
        case WM_CREATE: {
            CreateWindowA("STATIC", "Host", WS_CHILD|WS_VISIBLE, 8,8,35,20,h,nullptr,nullptr,nullptr);
            g_app.eHost=CreateWindowA("EDIT", "127.0.0.1", WS_CHILD|WS_VISIBLE|WS_BORDER, 45,8,100,20,h,nullptr,nullptr,nullptr);
            CreateWindowA("STATIC", "Port", WS_CHILD|WS_VISIBLE, 150,8,30,20,h,nullptr,nullptr,nullptr);
            g_app.ePort=CreateWindowA("EDIT", "5050", WS_CHILD|WS_VISIBLE|WS_BORDER, 182,8,55,20,h,nullptr,nullptr,nullptr);
            CreateWindowA("STATIC", "Room", WS_CHILD|WS_VISIBLE, 242,8,35,20,h,nullptr,nullptr,nullptr);
            g_app.eRoom=CreateWindowA("EDIT", "main", WS_CHILD|WS_VISIBLE|WS_BORDER, 280,8,90,20,h,nullptr,nullptr,nullptr);
            CreateWindowA("STATIC", "Pass", WS_CHILD|WS_VISIBLE, 375,8,35,20,h,nullptr,nullptr,nullptr);
            g_app.ePass=CreateWindowA("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 412,8,90,20,h,nullptr,nullptr,nullptr);
            CreateWindowA("BUTTON", "Join", WS_CHILD|WS_VISIBLE, 510,8,55,22,h,(HMENU)1,nullptr,nullptr);
            CreateWindowA("BUTTON", "Create", WS_CHILD|WS_VISIBLE, 570,8,60,22,h,(HMENU)2,nullptr,nullptr);
            CreateWindowA("BUTTON", "Clear", WS_CHILD|WS_VISIBLE, 635,8,55,22,h,(HMENU)3,nullptr,nullptr);
            g_app.lbl=CreateWindowA("STATIC", "Not connected", WS_CHILD|WS_VISIBLE, 8,35,700,20,h,nullptr,nullptr,nullptr);
            g_app.hwnd = h;
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(w)==1) clientConnectAndJoin(false);
            if (LOWORD(w)==2) clientConnectAndJoin(true);
            if (LOWORD(w)==3 && g_app.connected) sendLine(g_app.sock, "CLEAR");
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE: {
            if (!(w & MK_LBUTTON) && m == WM_MOUSEMOVE) return 0;
            int x = LOWORD(l)/CELL;
            int y = (HIWORD(l)-80)/CELL;
            if (x>=0 && x<BOARD_W && y>=0 && y<BOARD_H) {
                cx=x; cy=y;
                if (g_app.connected) sendLine(g_app.sock, "DRAW " + std::to_string(x) + " " + std::to_string(y) + " " + std::string(1,g_app.pen));
                InvalidateRect(h,nullptr,FALSE);
            }
            return 0;
        }
        case WM_NET_LINE: {
            std::unique_ptr<std::string> line((std::string*)l);
            std::stringstream ss(*line); std::string head; ss>>head;
            if (head=="ROOM") { std::string mode; ss>>g_app.room>>mode; setStatus("Joined " + g_app.room + " (" + mode + ")"); }
            else if (head=="STATE") { std::string payload; std::getline(ss,payload); g_app.board = boardFromWire(trim(payload)); InvalidateRect(h,nullptr,FALSE); }
            else if (head=="EVENT") {
                std::string k; ss>>k;
                if (k=="DRAW") { int x,y; char ch; ss>>x>>y>>ch; if(x>=0&&x<BOARD_W&&y>=0&&y<BOARD_H) g_app.board[y][x]=ch; }
                if (k=="CLEAR") g_app.board.assign(BOARD_H, std::string(BOARD_W,'.'));
                InvalidateRect(h,nullptr,FALSE);
            } else if (head=="ERROR") { std::string msg; std::getline(ss,msg); setStatus(trim(msg)); }
            return 0;
        }
        case WM_KEYDOWN:
            if (w=='P') g_app.pen=(g_app.pen=='#'?'@':'#');
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(h,&ps);
            for (int y=0;y<BOARD_H;++y) for(int x=0;x<BOARD_W;++x) drawCell(hdc,x,y,g_app.board[y][x],x==cx&&y==cy);
            EndPaint(h,&ps); return 0;
        }
        case WM_DESTROY:
            g_app.connected=false;
            if (g_app.sock != INVALID_SOCKET) closesocket(g_app.sock);
            if (g_app.reader.joinable()) g_app.reader.join();
            PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h,m,w,l);
}

} // namespace

int APIENTRY WinMain(HINSTANCE hi, HINSTANCE, LPSTR cmd, int show) {
    WSADATA w; if (WSAStartup(MAKEWORD(2,2), &w) != 0) return 1;

    std::string args = cmd ? cmd : "";
    if (args.find("--server") != std::string::npos) {
        int code = runServer(5050);
        WSACleanup();
        return code;
    }

    WNDCLASSA wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName="SharedWhiteboardUI";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA("SharedWhiteboardUI","Shared Whiteboard (Windows App)",WS_OVERLAPPEDWINDOW,
        100,100,860,430,nullptr,nullptr,hi,nullptr);
    ShowWindow(hwnd, show);

    MSG msg;
    while (GetMessage(&msg,nullptr,0,0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    WSACleanup();
    return 0;
}

#else
#include <iostream>
int main() {
    std::cout << "This project targets Windows GUI build only. Build SharedWhiteboard.exe on Windows.\n";
    return 0;
}
#endif
