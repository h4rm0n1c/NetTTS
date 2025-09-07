#include "help.hpp"
#include <windows.h>
#include <string>

// ---- help text ----
std::wstring get_help_text_w(){ // central source of truth
    // (Keep this short and accurate; main.cpp owns behavior.)
    // Current flags are based on the code in main/net_server/tts_engine.
    // WAV capture is disabled in the engine by design.
    const wchar_t* lines[] = {
        L"Usage: nettts_gui.exe [--gui|--headless] [--verbose]",
        L"                       [--host HOST] [--port N] [--devnum N]",
        L"                       [--vox | --voxclean] [--posn-ms N] [--selftest]",
        L"                       [--log C:\\path\\file.log]",
        L"",
        L"Options:",
        L"  --gui / --headless   Show the GUI window or run hidden (message pump always present)",
        L"  --verbose            Print runtime logs to the console (and keep them in the debugger)",
        L"  --host HOST          TCP host to bind (default 127.0.0.1)",
        L"  --port N             TCP port (default 5555)",
        L"  --devnum N           Output device number (-1 = default mapper)",
        L"  --vox                Enable VOX prosody (adds vendor tags; wraps with \\!wH1..\\!wH0)",
        L"  --voxclean           VOX prosody without wH wrap (no \\!wH1/\\!wH0; still adds \\!br, etc.)",
        L"  --posn-ms N          Enable periodic PosnGet polling every N ms (if the engine supports it)",
        L"  --selftest           Queue a short audible self-test matrix and speak it",
        L"  --log PATH           Also write logs to PATH (append mode not implemented)",
        L"  --help               Show this help and exit",
        L"",
        L"TCP commands (one per line; can be inline in the text stream):",
        L"  /rate N              One-shot vendor rate for the next phrase (e.g. N=160 -> \\!r1.60)",
        L"  /pitch N             One-shot vendor pitch for the next phrase (0..100 UI, maps to \\!%%)",
        L"  /pause ms            Insert a pause tag (e.g. 500 -> \\!sf500) and boundary",
        L"  /stop                Stop current speech",
        L"  /quit | /exit        Shutdown the server/app",
        L"",
        L"Inline markup:",
        L"  [[pause 500]]        In-band pause directive (transforms to \\!sf500 plus \\!br)",
        L"",
        L"Notes:",
        L"  • WAV capture is disabled; audio goes to the selected device.",
        L"  • In VOX modes, final cadence adds a ~500ms pause and a boundary.",
        nullptr
    };

    std::wstring out;
    for (const wchar_t** p = lines; *p; ++p){
        out += *p; out += L"\r\n";
    }
    return out;
}

// ---- extras: short usage + show-and-exit helpers ----
void usage_short(){
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    const wchar_t* s =
        L"Usage: nettts_gui.exe [--gui|--headless] [--verbose] "
        L"[--host HOST] [--port N] [--devnum N] [--vox|--voxclean]\r\n";
    if (h && h != INVALID_HANDLE_VALUE){
        DWORD w; WriteConsoleW(h, s, (DWORD)wcslen(s), &w, nullptr);
    }
}

void show_help_and_exit(bool error /*=false*/){
    // Ensure help text is visible even when launched without a console
    if (!log_has_console()) log_attach_console();
    print_help();
    ExitProcess(error ? 1u : 0u);
}


// ---- console help (always prints regardless of --verbose) ----
void print_help(){
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    auto txt = get_help_text_w();
    if(h && h != INVALID_HANDLE_VALUE){
        DWORD w; WriteConsoleW(h, txt.c_str(), (DWORD)txt.size(), &w, nullptr);
    }
}
