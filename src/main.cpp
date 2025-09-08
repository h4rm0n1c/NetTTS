#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <deque>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include "log.hpp"
#include "vox_parser.hpp"
#include "tts_engine.hpp"

#include "net_server.hpp"
#include "util.hpp"   // u8_to_w()

#include "ipc.hpp"
#include "gui.hpp"   // create_main_dialog(...)
#include "help.hpp"  // usage_short(), show_help_and_exit(), print_help()
#include <mmsystem.h>   // for --list-devices (WinMM)

#ifndef WM_APP
#  define WM_APP 0x8000
#endif
#define WM_APP_SPEAK             (WM_APP + 1)
#define WM_APP_TTS_TEXT_DONE     (WM_APP + 7)
#define WM_APP_TTS_TEXT_START    (WM_APP + 8)

// ------------------------------------------------------------------
// CLI state
static bool         g_show_window   = false;
static bool         g_headless      = false;
static bool         g_verbose       = false;
static std::wstring g_host          = L"127.0.0.1";
static int          g_port          = 5555;
static int          g_dev_index     = -1;
static int          g_posn_poll_ms  = 0;
static bool         g_selftest      = false;

// App state
static HWND         g_hwnd          = nullptr;
static Engine       g_eng;
static LONG         g_inflight_local= 0;

static bool g_vox_enabled = false;
static bool g_vox_clean   = false; 

static bool g_cli_gui   = false;  // --gui (modeless dialog on top of our hidden app window)
static bool g_cli_help  = false;  // --help (print/show help then exit)

// ------------------------------------------------------------------
// Chunk queue
struct Chunk { std::wstring text; };
static std::deque<Chunk> g_q;

// [[pause 500]]  →  " \!sf50 " and " \!br " boundary
static void expand_inline_pauses_and_enqueue(const std::string& line){
    size_t i=0, n=line.size();
    auto push_text = [&](const std::string& s){
        if (s.empty()) return;
        g_q.push_back({ u8_to_w(s) });
        g_q.push_back({ L" \\!br " }); // force boundary between logical chunks
    };
    while (i<n){
        size_t p = line.find("[[pause", i);
        if (p == std::string::npos) { push_text(line.substr(i)); break; }
        if (p > i) push_text(line.substr(i, p-i));

        size_t close = line.find("]]", p);
        int ms = 0;
        if (close != std::string::npos) {
            std::string inside = line.substr(p+2, close-(p+2));
            if (sscanf(inside.c_str(), "pause %d", &ms) != 1) ms = 0;
            ms = std::max(0, std::min(ms, 5000));
            int cs = (ms + 5) / 10;
            wchar_t tmp[64]; _snwprintf(tmp, 63, L" \\!sf%d ", cs);
            g_q.push_back({ tmp });
            g_q.push_back({ L" \\!br " });
            i = close + 2;
        } else {
            // malformed tail -> ignore
            break;
        }
    }
}

// Map leading “/rate N” and “/pitch N” to vendor tags (non-sticky)
static bool maybe_handle_inline_cmds(const std::string& line){
    auto is_space = [](char c){ return !!isspace((unsigned char)c); };
    size_t i=0, n=line.size(); while (i<n && is_space(line[i])) ++i;
    if (i>=n || line[i] != '/') return false;

    size_t j=i+1; while (j<n && !is_space(line[j])) ++j;
    std::string kw = line.substr(i+1, j-(i+1));
    for(char& c: kw) c=(char)tolower((unsigned char)c);
    auto rest = [&](size_t k){ while (k<n && is_space(line[k])) ++k; return k; };

    if (kw=="rate"){
        size_t p = rest(j); int val=0; bool have=false;
        while (p<n && isdigit((unsigned char)line[p])) { have=true; val = val*10 + (line[p]-'0'); ++p; }
        if (have){
            double scale = 2.0 - (1.9 * (std::min(std::max(val,0),100) / 100.0));
            wchar_t t[64]; _snwprintf(t,63,L" \\!R%.2f ", scale);
            g_q.push_back({ t }); g_q.push_back({ L" \\!br " });
            return true;
        }
    } else if (kw=="pitch"){
        size_t p = rest(j); int val=0; bool have=false;
        while (p<n && isdigit((unsigned char)line[p])) { have=true; val = val*10 + (line[p]-'0'); ++p; }
        if (have){
            double scale = 0.70 + (0.60 * (std::min(std::max(val,0),100)/100.0)); // ~0.70..1.30
            wchar_t t[64]; _snwprintf(t,63,L" \\!%%%.2f ", scale); // %% -> literal %
            g_q.push_back({ t }); g_q.push_back({ L" \\!br " });
            return true;
        }
    }else if (kw == "pause") {
        size_t p = rest(j); int ms=0; bool have=false;
        while (p<n && isdigit((unsigned char)line[p])) { have=true; ms = ms*10 + (line[p]-'0'); ++p; }
        if (have) {
           int cs = (std::min(std::max(ms,0),5000) + 5) / 10;
           wchar_t t[64]; _snwprintf(t,63,L" \\!sf%d  \\!br ", cs);
           g_q.push_back({ t });
            return true;
      }
    }
    return false;
}


// Send one queued chunk as-is (no internal \!br splitting).
// If the NEXT queued item is a standalone \!br, append it to the same speak
// so vendor pauses anchored to a boundary still work naturally.
static void kick_if_idle(){
    if (g_eng.inflight.load(std::memory_order_relaxed) > 0) return;
    if (g_q.empty()) return;

    auto is_just_br = [](const std::wstring& s)->bool{
        // trim spaces
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == L' ' || s[a] == L'\t')) ++a;
        while (b > a && (s[b-1] == L' ' || s[b-1] == L'\t')) --b;
        if (b - a != 4) return false; // "\!br" length
        return s.compare(a, 4, L"\\!br") == 0;
    };

    // take exactly one chunk
    std::wstring w = g_q.front().text;
    g_q.pop_front();

    // if next is a bare \!br, glue it
    if (!g_q.empty() && is_just_br(g_q.front().text)) {
        w += g_q.front().text;
        g_q.pop_front();
    }

    const bool tagged = text_looks_tagged(w);
    HRESULT hr = tts_speak(g_eng, w, tagged);
    if (g_verbose) {
        dprintf("[speak] hr=0x%08lx tagged=%d len=%u", hr, tagged?1:0, (unsigned)w.size());
    }
}




// Self-test matrix (audible probes; FlexTalk vendor tags)
static void enqueue_selftest(){
    auto add = [&](const std::wstring& W){
        g_q.push_back({ W });
        g_q.push_back({ L" \\!br " }); // separate each test audibly
    };

    // --- PAUSE TESTS (anchored with \!br so FlexTalk honors them) ---
    add(L"PAUSE P1. Final pause 500ms before next phrase. \\!sf500 \\!br Next phrase.");
    add(L"PAUSE P2. Final pause 1200ms before next phrase. \\!sf1200 \\!br Next phrase.");
    add(L"PAUSE P3. Initial pause 700ms between phrases. One. \\!br \\!si700 Two.");
    add(L"PAUSE P4. Start-of-utterance initial pause 1000ms. \\!br \\!si1000 This starts after one second.");
    // Negative control: no boundary after final-pause tag (often no clear pause)
    add(L"PAUSE P5. Final-pause tag without boundary (likely minimal effect). Before. \\!sf800 Then continues without forced break.");

    // --- RATE TESTS (FlexTalk: smaller R=faster, larger R=slower) ---
    add(L"RATE R1. One-phrase slow with \\!r1.60 then back to normal.");
    add(L"\\!R0.30 RATE R2. Sticky very fast rate. This should sound fast.");
    add(L"RATE R3. Still fast (sticky).");
    add(L"\\!R1.00 RATE R4. Reset rate to normal.");

    // --- PITCH TESTS ---
    add(L"\\!%0.85 PITCH P1. One-phrase lower pitch. Back to baseline after this.");
    add(L"\\!%%1.20 PITCH P2. Sticky higher pitch. Next line should also be higher.");
    add(L"PITCH P3. Still higher pitch (sticky). \\!%%1.00 Reset pitch to baseline.");

    // --- BOUNDARY ONLY (sanity) ---
    add(L"BOUNDARY B1. Before boundary. \\!br After boundary.");

    // --- TAGS WITHOUT SPACES (negative control) ---
    g_q.push_back({ L"TAGS D1. Tag without surrounding spaces (engine may ignore or speak literally)." });
    g_q.push_back({ L"\\!sf500" });               // intentionally glued
    g_q.push_back({ L" \\!br " });

    // --- COMBINED MID-SENTENCE FINAL PAUSE (clear example) ---
    add(L"COMBO C1. Hello \\!sf1000 \\!br world.");
}


// Posn polling
static UINT_PTR g_posn_timer = 0;
static void start_posn_poll(){ if (!g_posn_timer && g_posn_poll_ms>0) g_posn_timer = SetTimer(g_hwnd, 42, (UINT)g_posn_poll_ms, nullptr); }
static void stop_posn_poll(){ if (g_posn_timer){ KillTimer(g_hwnd, g_posn_timer); g_posn_timer=0; } }

// --- VOX debug logging (guarded by g_verbose) ---
static std::string replace_all(std::string s, const std::string& a, const std::string& b){
    size_t p = 0;
    while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
    return s;
}
static void log_vox_transform(const std::string& in_u8, const std::wstring& out_w){
    if (!g_verbose) return;
    std::string out_u8 = w_to_u8(out_w);

    // pretty for readability in logs (doesn't affect what we send to TTS)
    std::string pretty = out_u8;
    pretty = replace_all(pretty, "\\!wH1", "[wH1]");
    pretty = replace_all(pretty, "\\!wH0", "[wH0]");
    pretty = replace_all(pretty, "\\!br",  "[BR]");

    dprintf("[vox] in : \"%s\"", in_u8.c_str());
    dprintf("[vox] out: \"%s\"", out_u8.c_str());
    dprintf("[vox] viz: \"%s\"", pretty.c_str());
}


// Enqueue one inbound line, applying --vox if enabled.
static void enqueue_incoming_text(const std::string& line){
    if (g_vox_enabled) {
        // VOX: transform then push as a single chunk (no extra splitting)
        std::wstring w = u8_to_w(line);
        std::wstring wtag = vox_process(w,!g_vox_clean);
        std::wstring prefix = tts_vendor_prefix_from_ui();
        if (!prefix.empty()) wtag = prefix + wtag;
        log_vox_transform(line, wtag);     // <-- add this line
        if (!wtag.empty()) g_q.push_back({ wtag });
    } else {
        // Non-VOX: keep your existing inline handling
        if (!maybe_handle_inline_cmds(line))
            expand_inline_pauses_and_enqueue(line);
    }
    kick_if_idle();
}

// ------------------------------------------------------------------
// WndProc
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){

case WM_APP_ATTRS:{
    auto* p = (GuiAttrs*)l;
    if (p){
        tts_set_volume_percent(g_eng,p->vol_percent);
        tts_set_rate_percent_ui(p->rate_percent);
        tts_set_pitch_percent_ui(p->pitch_percent);
        delete p;
    }
    return 0;
}

case WM_APP_DEVICE: {
    auto* p = (GuiDeviceSel*)l; // struct from gui.hpp (index)
    if (p){
        int new_idx = p->index; // -1 = default mapper
        if (new_idx != g_dev_index){
            g_dev_index = new_idx;
            // Re-init the engine on the new device
            tts_init(g_eng, g_dev_index);
        }
        // reflect back to GUI so the combo shows the actual device
        if (HWND dlg = gui_get_main_hwnd()){
            PostMessageW(dlg, WM_APP_DEVICE_STATE, (WPARAM)g_dev_index, 0);
        }
        delete p;
    }
    return 0;
}

case WM_APP_SERVER_REQ: {
    auto* r = (GuiServerReq*)l; // struct from gui.hpp (host/port/start)
    if (r){
        bool ok = false;
        if (r->start){
            ok = server_start(std::wstring(r->host), r->port, g_hwnd);
        } else {
            server_stop();
            ok = true;
        }
        if (HWND dlg = gui_get_main_hwnd()){
            PostMessageW(dlg, WM_APP_SERVER_STATE, ok && r->start ? 1 : 0, 0);
        }
        delete r;
    }
    return 0;
}

case WM_APP_GET_DEVICE:{
    // Return your current device index (use your own tracker here)
    // Example if you track it as g_devnum:
    // return (LRESULT)g_devnum;
    return (LRESULT)g_dev_index;
}

case WM_APP_STOP: {
    // Hard stop: clear pending queue and reset audio so current utterance halts
    while (!g_q.empty()) g_q.pop_front();
    tts_audio_reset(g_eng);               // immediate stop/reset (SAPI4)
    g_eng.inflight.store(0);              // best-effort local reset
    gui_notify_tts_state(false);          // reflect back to GUI
    if (g_verbose) dprintf("[stop] hard stop + clear queue");
    return 0;
}


case WM_APP_SPEAK: {
    std::string* txt = (std::string*)l;
    if (!txt) return 0;
    enqueue_incoming_text(*txt);
    delete txt;
    return 0;
}

case WM_APP_TTS_TEXT_START:
    g_inflight_local++;
    // one-liner: tell the GUI it's busy now
    gui_notify_tts_state(true);
    return 0;


case WM_APP_TTS_TEXT_DONE: {
    if (g_inflight_local > 0) g_inflight_local--;
    tts_file_flush(g_eng); // (no-op for device mode)
    if (g_eng.inflight.load(std::memory_order_relaxed) == 0) {
        kick_if_idle();
        if (g_eng.inflight.load(std::memory_order_relaxed) == 0 && g_q.empty()) {
            gui_notify_tts_state(false);  // <— GUI button → "Speak"
        }
    }
    return 0;
}



    case WM_TIMER:
    if (w == g_posn_timer) {
        if (tts_supports_posn(g_eng)) {
            DWORD p=0; 
            if (tts_posn_get(g_eng, &p)) {
                dprintf("[posn] %lu", (unsigned long)p);
            }
        }
        return 0;
    }

        break;

    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        stop_posn_poll();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ------------------------------------------------------------------
// CLI
static void parse_cmdline(){
    int argc=0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i=1;i<argc;i++){
        std::wstring a = argv[i];
        if (a==L"--gui") { g_cli_gui = true; } else if (a==L"--help" || a == L"-h" || a == L"/?") { g_cli_help = true; }
else if (_wcsicmp(argv[i], L"--list-devices") == 0) {
    if (!g_verbose) AllocConsole(); // or your own console attach
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    auto put = [&](const std::wstring& s){
        DWORD w; WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &w, nullptr);
        WriteConsoleW(h, L"\r\n", 2, &w, nullptr);
    };
    put(L"Device index mapping (use with --devnum):");
    put(L"  -1 : (Default output device)");
    UINT ndev = waveOutGetNumDevs();
    for (UINT di=0; di<ndev; di++){
        WAVEOUTCAPSW caps{}; waveOutGetDevCapsW(di, &caps, sizeof(caps));
        wchar_t line[512]; _snwprintf(line, 511, L"  %u : %ls", di, caps.szPname);
        put(line);
    }
    ExitProcess(0);
}
        else if (a==L"--headless") g_headless=true;
        else if (a==L"--verbose") g_verbose=true;
        else if (a==L"--vox"){ g_vox_enabled = true; }
        else if (a==L"--voxclean") { g_vox_enabled = true; g_vox_clean = true; }
        else if (a==L"--host" && i+1<argc) g_host = argv[++i];
        else if (a==L"--port" && i+1<argc) g_port = _wtoi(argv[++i]);
        else if (a==L"--devnum" && i+1<argc) g_dev_index = _wtoi(argv[++i]);
        else if (a==L"--posn-poll-ms" && i+1<argc) g_posn_poll_ms = _wtoi(argv[++i]);
        else if (a==L"--selftest") g_selftest=true;
    }
    LocalFree(argv);
}

// ------------------------------------------------------------------
// WinMain
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int){
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    parse_cmdline();

// Re-attach/allocate a console so printf/dprintf show up even with -mwindows
log_attach_console();
// Honor --verbose (or --console) like before
log_set_verbose(g_verbose);

    
    if (g_cli_help) {
        show_help_and_exit(false);   // prints full help (and attaches a console if needed)
        return 0;                    // (defensive; show_help_and_exit() already exits)
    }


    // Window
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpszClassName = L"NetTTS.EventHandler";
    wc.hInstance     = hInst;
    wc.lpfnWndProc   = WndProc;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP),
                                 IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP),
                                 IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"NetTTS Eventhandler",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 200, 200,
                             nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hwnd, SW_HIDE);

    if (g_cli_gui) {
        HWND hDlg = create_main_dialog(hInst, g_hwnd);
        gui_set_app_hwnd(g_hwnd);


        // Seed: current device index -> combo selection
        if (hDlg){
            PostMessageW(hDlg, WM_APP_DEVICE_STATE, (WPARAM)g_dev_index, 0);

            // Seed: host/port fields from CLI (g_host, g_port)
            auto* f = new GuiServerFields{};
            wcsncpy(f->host, g_host.c_str(), 63); f->host[63]=0;
            f->port = g_port;
            PostMessageW(hDlg, WM_APP_SET_SERVER_FIELDS, 0, (LPARAM)f);

            // Seed: server button from actual running state
            PostMessageW(hDlg, WM_APP_SERVER_STATE, server_is_running() ? 1 : 0, 0);
        }
    }

    if (!tts_init(g_eng, g_dev_index)){
        MessageBeep(MB_ICONERROR);
        return 2;
    }
    tts_set_notify_hwnd(g_eng, g_hwnd);

    if (g_headless){
        server_start(g_host, g_port, g_hwnd);
    }

    if (g_posn_poll_ms > 0 && tts_supports_posn(g_eng)) start_posn_poll();

    if (g_selftest){
        enqueue_selftest();
        kick_if_idle();
        // literal (untagged) demo
        std::wstring literal = L"Untagged literal: \\!sf30 should be spoken literally.";
        (void)tts_speak(g_eng, literal, /*force_tagged*/false);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0){
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    tts_shutdown(g_eng);
    CoUninitialize();
    return 0;
}
