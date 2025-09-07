#include "help.hpp"
#include <windows.h>
#include <string>

// ---- help text ----
std::wstring get_help_text_w(){   // was build_help_text_w()
    const wchar_t* lines[] = {
        L"Usage: nettts_gui.exe [--gui|--headless] [--verbose] [--host HOST] [--port N]",
        L"                      [--devnum N] [--file C:\\out.wav] [--ansi]",
        L"                      [--oneshot \"text\" [--hold-ms N]] [--scale-100|--scale-65535]",
        L"                      [--pitch-vendor|--pitch-sapi] [--log C:\\path\\file.log]",
        L"",
        L"Options:",
        L"  --gui / --headless      Show GUI window or run hidden (message pump always present)",
        L"  --verbose               Print runtime logs to the console",
        L"  --host HOST             TCP host to bind (default 127.0.0.1)",
        L"  --port N                TCP port (default 5555)",
        L"  --devnum N              Output device number (0..N-1)",
        L"  --file PATH             Write WAV instead of playing to device",
        L"  --ansi                  Use ITTSCentralA instead of W",
        L"  --oneshot \"text\"         Speak this text then exit (works with --file)",
        L"  --hold-ms N             Keep process alive N ms after oneshot",
        L"  --pitch-vendor|--pitch-sapi  FlexTalk vendor pitch vs SAPI Pitch",
        L"  --log PATH              Write logs to the given file",
        L"  --help                  Show this help and exit",
        L"",
        L"TCP commands (one per line, can be inline):",
        L"  /rate N        Set rate (0..100 UI)",
        L"  /pitch N       Set pitch (0..100 UI). Vendor mode injects \\!%%<factor>",
        L"  /stop          Stop current speech",
        L"  /quit,/exit    Shutdown the server/app",
        nullptr
    };
    std::wstring out;
    for (const wchar_t** p = lines; *p; ++p){ out += *p; out += L"\r\n"; }
    return out;
}

// ---- console help (always prints regardless of --verbose) ----
void print_help(){
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    auto txt = get_help_text_w();
    if(h && h != INVALID_HANDLE_VALUE){
        DWORD w; WriteConsoleW(h, txt.c_str(), (DWORD)txt.size(), &w, nullptr);
    }
}
