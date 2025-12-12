#include "help.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <cwchar>
#include <string>

namespace {

std::wstring build_device_mapping_text(){
    std::wstring map;
    map += L"Device index mapping (use with --devnum):\r\n";
    map += L"  -1 : (Default output device)\r\n";

    const UINT ndev = waveOutGetNumDevs();
    if (ndev == 0) {
        map += L"  (No output devices found)\r\n";
        return map;
    }

    for (UINT di = 0; di < ndev; ++di){
        WAVEOUTCAPSW caps{};
        if (waveOutGetDevCapsW(di, &caps, sizeof(caps)) == MMSYSERR_NOERROR){
            wchar_t line[512]; _snwprintf(line, 511, L"  %u : %ls\r\n", di, caps.szPname);
            map += line;
        }
    }
    return map;
}

std::wstring g_device_mapping_text;
bool g_device_mapping_ready = false;

} // namespace

std::wstring get_device_mapping_text(){
    if (!g_device_mapping_ready){
        g_device_mapping_text = build_device_mapping_text();
        g_device_mapping_ready = true;
    }
    return g_device_mapping_text;
}

// ---- help text ----
std::wstring get_help_text_w(){ // central source of truth
    // (Keep this short and accurate; main.cpp owns behavior.)
    // Current flags are based on the code in main/net_server/tts_engine.
    const wchar_t* lines[] = {
        L"Usage: nettts_gui.exe [--startserver] [--headless]",
        L"                       [--host HOST] [--port N] [--devnum N]",
        L"                       [--vox | --voxclean] [--posn-ms N] [--selftest]",
        L"                       [--status-port N] [--log C:\\path\\file.log]",
        L"",
        L"Options:",
        L"  --startserver        Start the TCP server",
        L"  --headless           Run without the GUI (console mode; prints runtime logs)",
        L"  --host HOST          TCP host to bind (default 127.0.0.1)",
        L"  --port N             TCP port (default 5555)",
        L"  --status-port N      Status server TCP port (default --port+1)",
        L"  --list-devices        Print output device indices and names, then exit",
        L"  --devnum N           Output device number (-1 = default mapper)",
        L"  --vox                Enable VOX prosody (adds vendor tags; wraps with \\!wH1..\\!wH0)",
        L"  --voxclean           VOX prosody without wH wrap (no \\!wH1/\\!wH0; still adds \\!br, etc.)",
        L"  --posn-ms N          Enable periodic PosnGet polling every N milliseconds (if the engine supports it)",
        L"  --selftest           Queue a short audible self-test matrix and speak it",
        L"  --log PATH           Also write logs to PATH (append mode not implemented)",
        L"  --help               Show this help and exit",
        L"",
        L"FlexTalk vendor tags:",
        L"  \\!wH1 / \\!wH0     Enable/disable VOX prosody",
        L"  \\!sfN              Final pause of N cs (e.g., 500 -> \\!sf500)",
        L"  \\!siN              Initial pause of N cs",
        L"  \\!R#              Rate scale (smaller=fast, larger=slow)",
        L"  \\!%#              Pitch scale (0.70..1.30 approx)",
        L"  \\!br              Explicit boundary",
        L"",
        L"TCP commands (one per line; can be inline in the text stream):",
        L"  /rate N              Set rate (0..200, 100=1.00)",
        L"  /pitch N             Set pitch (0..200, 100=1.00)",
        L"  /pause ms            Insert a pause tag (e.g. 500 -> \\!sf500) and boundary",
        L"  /stop                Stop current speech",
        L"  /quit | /exit        Shutdown the server/app",
        L"",
        L"Inline markup:",
        L"  [[pause 500]]        In-band pause directive (transforms to \\!sf500 plus \\!br)",
        L"",
        L"Notes:",
        L"  * In VOX modes, final cadence adds a ~500ms pause and a boundary.",
        nullptr
    };

    std::wstring out;
    for (const wchar_t** p = lines; *p; ++p){
        out += *p; out += L"\r\n";
    }

    out += L"\r\n";
    out += get_device_mapping_text();
    return out;
}

// ---- extras: short usage + show-and-exit helpers ----
void usage_short(){
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    const wchar_t* s =
        L"Usage: nettts_gui.exe [--startserver] [--headless] "
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


// ---- console help (always prints regardless of --headless) ----
void print_help(){
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    auto txt = get_help_text_w();
    if(h && h != INVALID_HANDLE_VALUE){
        DWORD w; WriteConsoleW(h, txt.c_str(), (DWORD)txt.size(), &w, nullptr);
    }
}
