#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include "log.hpp"
#include "net_server.hpp"
#include "util.hpp"
#include <string>
#include <atomic>

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

// expose status to the rest of the app
bool server_is_running(){
    return g_server_running.load(std::memory_order_acquire);
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
