#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include "log.hpp"
#include "net_server.hpp"
#include "util.hpp"
#include <string>
#include <atomic>
#include <vector>

#ifndef WM_APP
#  define WM_APP 0x8000
#endif
#ifndef WM_APP_SPEAK
#  define WM_APP_SPEAK (WM_APP+1)
#endif

static HANDLE g_hThread = nullptr;
static volatile LONG g_stop = 0;
static SOCKET g_listen = INVALID_SOCKET;
static HWND   g_hwnd   = nullptr;
static std::wstring g_host = L"127.0.0.1";
static int          g_port = 5555;

static HANDLE g_status_thread = nullptr;
static volatile LONG g_status_stop = 0;
static SOCKET g_status_listen = INVALID_SOCKET;
static std::wstring g_status_host = L"127.0.0.1";
static int          g_status_port = 5556;
static CRITICAL_SECTION g_status_cs;
static bool g_status_cs_init = false;
static std::vector<SOCKET> g_status_clients;

using inet_pton_func = INT (WSAAPI*)(INT, const char*, void*);

static bool inet_pton_compat(const std::string& host, in_addr* out)
{
    if (!out) {
        return false;
    }

    static inet_pton_func cached = nullptr;
    static bool attempted = false;

    if (!attempted) {
        HMODULE module = GetModuleHandleW(L"ws2_32.dll");
        if (!module) {
            module = LoadLibraryW(L"ws2_32.dll");
        }
        if (module) {
            cached = reinterpret_cast<inet_pton_func>(GetProcAddress(module, "inet_pton"));
        }
        attempted = true;
    }

    if (cached) {
        in_addr tmp{};
        if (cached(AF_INET, host.c_str(), &tmp) == 1) {
            *out = tmp;
            return true;
        }
    }

    return false;
}

static bool parse_ipv4(const std::string& host, in_addr* out)
{
    if (!out) {
        return false;
    }

    if (inet_pton_compat(host, out)) {
        return true;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    int addr_len = sizeof(addr);
    if (WSAStringToAddressA(const_cast<char*>(host.c_str()), AF_INET, nullptr,
                            reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0) {
        *out = addr.sin_addr;
        return true;
    }

    unsigned long raw = inet_addr(host.c_str());
    if (raw != INADDR_NONE || host == "255.255.255.255") {
        out->S_un.S_addr = raw;
        return true;
    }

    return false;
}

// ---- server state ----
static std::atomic<bool> g_server_running{false};
static std::atomic<bool> g_status_running{false};

// expose status to the rest of the app
bool server_is_running(){
    return g_server_running.load(std::memory_order_acquire);
}

bool status_server_is_running(){
    return g_status_running.load(std::memory_order_acquire);
}

static DWORD WINAPI server_thread(LPVOID){
    WSADATA wsa; 
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        dprintf("[net] WSAStartup failed");
        return 0;
    }

    sockaddr_in addr{}; 
    addr.sin_family = AF_INET; 
    addr.sin_port   = htons( (u_short)g_port );

    std::string hostA = w_to_u8(g_host);
    if (!parse_ipv4(hostA, &addr.sin_addr)) {
        dprintf("[net] failed to parse host '%s', using 127.0.0.1", hostA.c_str());
        if (!parse_ipv4("127.0.0.1", &addr.sin_addr)) {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
    }

    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(g_listen==INVALID_SOCKET){ 
        dprintf("[net] socket() failed"); 
        WSACleanup();
        return 0; 
    }

    int opt=1; 
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if(bind(g_listen,(sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){ 
        dprintf("[net] bind() failed"); 
        closesocket(g_listen); g_listen=INVALID_SOCKET; 
        WSACleanup(); 
        return 0; 
    }
    if(listen(g_listen, 4)==SOCKET_ERROR){ 
        dprintf("[net] listen() failed"); 
        closesocket(g_listen); g_listen=INVALID_SOCKET; 
        WSACleanup(); 
        return 0; 
    }
    g_server_running.store(true, std::memory_order_release);
    dprintf("[net] listening on %s:%d", hostA.c_str(), g_port);

    while(!g_stop){
        fd_set rf; FD_ZERO(&rf); FD_SET(g_listen, &rf);
        timeval tv{0, 200*1000};
        int r = select(0, &rf, nullptr, nullptr, &tv);
        if(r<=0){ continue; }
        sockaddr_in cli; int clen=sizeof(cli);
        SOCKET s = accept(g_listen,(sockaddr*)&cli,&clen);
        if(s==INVALID_SOCKET) continue;
        dprintf("[net] client connected");

        std::string buf; buf.reserve(1024);
        char tmp[512];
        for(;;){
            int n = recv(s,tmp,sizeof(tmp),0);
            if(n<=0) break;
            buf.append(tmp, tmp+n);
            size_t pos;
            while((pos = buf.find('\n')) != std::string::npos){
                std::string line = buf.substr(0,pos);
                buf.erase(0,pos+1);
                if(!line.empty() && line.back()=='\r') line.pop_back();
                std::string* heap = new std::string(std::move(line));
                PostMessageW(g_hwnd, WM_APP_SPEAK, 0, (LPARAM)heap);
            }
        }
        closesocket(s);
        dprintf("[net] client disconnected");
    }

    if(g_listen!=INVALID_SOCKET){ closesocket(g_listen); g_listen=INVALID_SOCKET; }
    WSACleanup();
    return 0;
}

static void status_remove_client_locked(std::vector<SOCKET>& clients, size_t idx){
    if (idx >= clients.size()) return;
    closesocket(clients[idx]);
    clients.erase(clients.begin() + idx);
}

static DWORD WINAPI status_server_thread(LPVOID){
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        dprintf("[status] WSAStartup failed");
        return 0;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons( (u_short)g_status_port );

    std::string hostA = w_to_u8(g_status_host);
    if (!parse_ipv4(hostA, &addr.sin_addr)) {
        dprintf("[status] failed to parse host '%s', using 127.0.0.1", hostA.c_str());
        if (!parse_ipv4("127.0.0.1", &addr.sin_addr)) {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
    }

    g_status_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(g_status_listen==INVALID_SOCKET){
        dprintf("[status] socket() failed");
        WSACleanup();
        return 0;
    }

    int opt=1;
    setsockopt(g_status_listen, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if(bind(g_status_listen,(sockaddr*)&addr,sizeof(addr))==SOCKET_ERROR){
        dprintf("[status] bind() failed");
        closesocket(g_status_listen); g_status_listen=INVALID_SOCKET;
        WSACleanup();
        return 0;
    }
    if(listen(g_status_listen, 4)==SOCKET_ERROR){
        dprintf("[status] listen() failed");
        closesocket(g_status_listen); g_status_listen=INVALID_SOCKET;
        WSACleanup();
        return 0;
    }
    g_status_running.store(true, std::memory_order_release);
    dprintf("[status] listening on %s:%d", hostA.c_str(), g_status_port);

    while(!g_status_stop){
        fd_set rf; FD_ZERO(&rf); FD_SET(g_status_listen, &rf);
        {
            if (g_status_cs_init) EnterCriticalSection(&g_status_cs);
            for (SOCKET s : g_status_clients){
                FD_SET(s, &rf);
            }
            if (g_status_cs_init) LeaveCriticalSection(&g_status_cs);
        }
        timeval tv{0, 200*1000};
        int r = select(0, &rf, nullptr, nullptr, &tv);
        if(r<=0){ continue; }

        if(FD_ISSET(g_status_listen, &rf)){
            sockaddr_in cli; int clen=sizeof(cli);
            SOCKET s = accept(g_status_listen,(sockaddr*)&cli,&clen);
            if(s!=INVALID_SOCKET){
                if (g_status_cs_init) EnterCriticalSection(&g_status_cs);
                g_status_clients.push_back(s);
                if (g_status_cs_init) LeaveCriticalSection(&g_status_cs);
                dprintf("[status] client connected");
            }
        }

        if (g_status_cs_init) EnterCriticalSection(&g_status_cs);
        for (size_t i = 0; i < g_status_clients.size(); ){
            SOCKET s = g_status_clients[i];
            if (FD_ISSET(s, &rf)){
                char tmp[1];
                int n = recv(s,tmp,sizeof(tmp),0);
                if(n<=0){
                    dprintf("[status] client disconnected");
                    status_remove_client_locked(g_status_clients, i);
                    continue;
                }
            }
            ++i;
        }
        if (g_status_cs_init) LeaveCriticalSection(&g_status_cs);
    }

    if(g_status_listen!=INVALID_SOCKET){ closesocket(g_status_listen); g_status_listen=INVALID_SOCKET; }
    if (g_status_cs_init) EnterCriticalSection(&g_status_cs);
    for (SOCKET s : g_status_clients){ closesocket(s); }
    g_status_clients.clear();
    if (g_status_cs_init) LeaveCriticalSection(&g_status_cs);
    WSACleanup();
    g_status_running.store(false, std::memory_order_release);
    return 0;
}

bool server_start(const std::wstring& host, int port, HWND hwnd){
    if(g_hThread) return true;
    g_stop=0; g_hwnd=hwnd; g_host=host; g_port=port;
    g_hThread = CreateThread(nullptr,0,server_thread,nullptr,0,nullptr);

    return g_hThread!=nullptr;
}
void server_stop(){
    g_stop=1;
    if(g_listen!=INVALID_SOCKET){ shutdown(g_listen, SD_BOTH); }
    if(g_hThread){ WaitForSingleObject(g_hThread, 2000); CloseHandle(g_hThread); g_hThread=nullptr; }
    g_server_running.store(false, std::memory_order_release);
}

bool status_server_start(const std::wstring& host, int port){
    if (g_status_thread){
        DWORD wait_result = WaitForSingleObject(g_status_thread, 0);
        if (wait_result == WAIT_TIMEOUT){
            if (status_server_is_running()){
                return true; // already running
            }

            // Thread handle is alive but not reporting a running server; try to stop it.
            dprintf("[status] stale status thread detected; restarting");
            g_status_stop = 1;
            if(g_status_listen!=INVALID_SOCKET){ shutdown(g_status_listen, SD_BOTH); }
            WaitForSingleObject(g_status_thread, 2000);
        } else if (status_server_is_running()){
            return true; // thread completed but server still marked running (should be rare)
        }

        CloseHandle(g_status_thread);
        g_status_thread = nullptr;
    } else if (status_server_is_running()){
        return true;
    }
    if (!g_status_cs_init){ InitializeCriticalSection(&g_status_cs); g_status_cs_init = true; }
    g_status_stop = 0; g_status_host = host; g_status_port = port;
    g_status_thread = CreateThread(nullptr,0,status_server_thread,nullptr,0,nullptr);
    return g_status_thread!=nullptr;
}

void status_server_stop(){
    g_status_stop = 1;
    if(g_status_listen!=INVALID_SOCKET){ shutdown(g_status_listen, SD_BOTH); }
    if(g_status_thread){ WaitForSingleObject(g_status_thread, 2000); CloseHandle(g_status_thread); g_status_thread=nullptr; }
    g_status_running.store(false, std::memory_order_release);
}

void status_server_broadcast(const char* msg, size_t len){
    if (!msg || len == 0) return;
    if (g_status_cs_init) EnterCriticalSection(&g_status_cs);
    for (size_t i = 0; i < g_status_clients.size(); ){
        SOCKET s = g_status_clients[i];
        int sent = send(s, msg, (int)len, 0);
        if (sent <= 0){
            status_remove_client_locked(g_status_clients, i);
            dprintf("[status] client disconnected");
            continue;
        }
        ++i;
    }
    if (g_status_cs_init) LeaveCriticalSection(&g_status_cs);
}
