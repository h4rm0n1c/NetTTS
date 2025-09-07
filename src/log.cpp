#include "log.hpp"
#include <cstdio>
#include <cstdarg>
#include <string>

static bool         g_has_console = false;
static bool         g_verbose     = false;   // NEW
static std::wstring g_logPath;
static FILE*        g_logFile = nullptr;

void log_set_path(const std::wstring& path){
    g_logPath = path;
    if(!g_logPath.empty() && !g_logFile){
        g_logFile = _wfopen(g_logPath.c_str(), L"w");
    }
}

void log_set_verbose(bool on){ g_verbose = on; }   // NEW
bool log_has_console(){ return g_has_console; }

void log_attach_console(){
    if(g_has_console) return;
    if(!AttachConsole(ATTACH_PARENT_PROCESS)){
        AllocConsole(); // fallback: create a new console window
    }
    g_has_console = true;
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
}

void dprintf(const char* fmt, ...){
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf)-1, fmt, ap);
    buf[sizeof(buf)-1] = 0;
    va_end(ap);

    OutputDebugStringA(buf);
    OutputDebugStringA("\r\n");

    // Only write to the console when verbose is enabled
    if(g_has_console && g_verbose){
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteConsoleA(h, buf, (DWORD)strlen(buf), &w, nullptr);
            WriteConsoleA(h, "\r\n", 2, &w, nullptr);
        }
    }
    if (g_logFile) { fprintf(g_logFile, "%s\n", buf); fflush(g_logFile); }
}
