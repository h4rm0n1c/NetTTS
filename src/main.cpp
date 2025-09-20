#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <deque>
#include <utility>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include "log.hpp"
#include "app_cli.hpp"
#include "app_bootstrap.hpp"
#include "app_window.hpp"
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
static bool         g_runserver     = false; // --startserver
static bool         g_headless      = false; // --headless
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

static bool g_cli_help  = false;  // --help (print/show help then exit)

// ------------------------------------------------------------------
// Chunk queue
struct Chunk { std::wstring text; };
static std::deque<Chunk> g_q;

static void push_chunk(std::wstring text){
    if (g_headless){
        std::string u8 = w_to_u8(text);
        dprintf("[queue] push: \"%s\"", u8.c_str());
    }
    g_q.push_back({ std::move(text) });
}

// [[pause 500]]  →  " \!sf50 " and " \!br " boundary
static void expand_inline_pauses_and_enqueue(const std::string& line){
    size_t i=0, n=line.size();
    auto push_text = [&](const std::string& s){
        if (s.empty()) return;
        push_chunk(u8_to_w(s));
        push_chunk(L" \\!br "); // force boundary between logical chunks
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
            push_chunk(tmp);
            push_chunk(L" \\!br ");
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
            double scale = std::min(std::max(val,0),200) / 100.0;
            wchar_t t[64]; _snwprintf(t,63,L" \\!R%.2f ", scale);
            push_chunk(t);
            push_chunk(L" \\!br ");
            return true;
        }
    } else if (kw=="pitch"){
        size_t p = rest(j); int val=0; bool have=false;
        while (p<n && isdigit((unsigned char)line[p])) { have=true; val = val*10 + (line[p]-'0'); ++p; }
        if (have){
            double scale = std::min(std::max(val,0),200) / 100.0;
            wchar_t t[64]; _snwprintf(t,63,L" \\!%%%.2f ", scale); // %% -> literal %
            push_chunk(t);
            push_chunk(L" \\!br ");
            return true;
        }
    }else if (kw == "pause") {
        size_t p = rest(j); int ms=0; bool have=false;
        while (p<n && isdigit((unsigned char)line[p])) { have=true; ms = ms*10 + (line[p]-'0'); ++p; }
        if (have) {
           int cs = (std::min(std::max(ms,0),5000) + 5) / 10;
           wchar_t t[64]; _snwprintf(t,63,L" \\!sf%d  \\!br ", cs);
           push_chunk(t);
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

    std::wstring prefix = tts_vendor_prefix_from_ui();
    if (!prefix.empty()) {
        w = prefix + w;
    }

    if (g_headless) {
        std::string payload = w_to_u8(w);
        dprintf("[speak] text=\"%s\"", payload.c_str());
    }

    const bool tagged = text_looks_tagged(w);
    HRESULT hr = tts_speak(g_eng, w, tagged);
    if (g_headless) {
        dprintf("[speak] hr=0x%08lx tagged=%d len=%u", hr, tagged?1:0, (unsigned)w.size());
    }
}




// Self-test matrix (audible probes; FlexTalk vendor tags)
static void enqueue_selftest(){
    auto add = [&](const std::wstring& W){
        push_chunk(W);
        push_chunk(L" \\!br "); // separate each test audibly
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
    push_chunk(L"TAGS D1. Tag without surrounding spaces (engine may ignore or speak literally).");
    push_chunk(L"\\!sf500");               // intentionally glued
    push_chunk(L" \\!br ");

    // --- COMBINED MID-SENTENCE FINAL PAUSE (clear example) ---
    add(L"COMBO C1. Hello \\!sf1000 \\!br world.");
}


// Posn polling
static UINT_PTR g_posn_timer = 0;
static void start_posn_poll(){ if (!g_posn_timer && g_posn_poll_ms>0) g_posn_timer = SetTimer(g_hwnd, 42, (UINT)g_posn_poll_ms, nullptr); }
static void stop_posn_poll(){ if (g_posn_timer){ KillTimer(g_hwnd, g_posn_timer); g_posn_timer=0; } }

static bool on_common_timer(HWND, UINT_PTR timer_id, void* /*user*/);
static void on_common_destroy(HWND, void* /*user*/);

static const AppWindowCallbacks kWindowCallbacks = {
    on_common_timer,
    nullptr,
    on_common_destroy,
    nullptr
};

// --- VOX debug logging (guarded by g_headless) ---
static std::string replace_all(std::string s, const std::string& a, const std::string& b){
    size_t p = 0;
    while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
    return s;
}
static void log_vox_transform(const std::string& in_u8, const std::wstring& out_w){
    if (!g_headless) return;
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
    if (g_headless){
        dprintf("[input] raw=\"%s\"", line.c_str());
    }

    auto is_space = [](char c){ return !!isspace((unsigned char)c); };
    size_t i=0, n=line.size(); while (i<n && is_space(line[i])) ++i;
    if (i < n && line[i] == '/'){
        size_t j=i+1; while (j<n && !is_space(line[j])) ++j;
        std::string kw = line.substr(i+1, j-(i+1));
        for (char& c : kw) c = (char)tolower((unsigned char)c);
        auto rest = [&](size_t k){ while (k<n && is_space(line[k])) ++k; return k; };
        if (kw=="stop"){
            PostMessageW(g_hwnd, WM_APP_STOP, 0, 0);
            return;
        } else if (kw=="rate" || kw=="pitch"){
            size_t p = rest(j);
            double val=0.0; bool ok=false;
            if (p < n){
                val = atof(line.c_str() + p); // accepts 1.5 or 150
                if (val <= 2.0) val *= 100.0;
                ok = true;
            }
            if (ok){
                int pct = (int)(val + 0.5);
                if (pct < 0) pct = 0; if (pct > 200) pct = 200;
                if (kw=="rate"){
                    tts_set_rate_percent_ui(pct);
                    if (HWND dlg = gui_get_main_hwnd()) PostMessageW(dlg, WM_APP_RATE_STATE, pct, 0);
                }else{
                    tts_set_pitch_percent_ui(pct);
                    if (HWND dlg = gui_get_main_hwnd()) PostMessageW(dlg, WM_APP_PITCH_STATE, pct, 0);
                }
            }
            return;
        }
    }

    if (g_vox_enabled) {
        // VOX: transform then push as a single chunk (no extra splitting)
        std::wstring w = u8_to_w(line);
        std::wstring wtag = vox_process(w,!g_vox_clean);
        log_vox_transform(line, wtag);
        if (!wtag.empty()) push_chunk(std::move(wtag));
    } else {
        // Non-VOX: keep your existing inline handling
        if (!maybe_handle_inline_cmds(line))
            expand_inline_pauses_and_enqueue(line);
    }
    if (HWND dlg = gui_get_main_hwnd()){
        auto* s = new std::string(line);
        PostMessageW(dlg, WM_APP_SET_TEXT, 0, (LPARAM)s);
    }
    kick_if_idle();
}

// ------------------------------------------------------------------
// WndProc
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l){
    AppWindowHandled common = handle_common_window_messages(h, m, w, l, &kWindowCallbacks);
    if (common.handled){
        return common.result;
    }

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
            if (tts_init(g_eng, g_dev_index)){
                tts_set_notify_hwnd(g_eng, g_hwnd);
            }
        }
        // reflect back to GUI so the combo shows the actual device
        if (HWND dlg = gui_get_main_hwnd()){
            PostMessageW(dlg, WM_APP_DEVICE_STATE, (WPARAM)g_dev_index, 0);
        }
        delete p;
    }
    return 0;
}

case WM_APP_PROSODY: {
    int mode = (int)w;
    if (mode == 0){ g_vox_enabled = false; g_vox_clean = false; tts_speak(g_eng, L" \\!wH0 ", true); }
    else if (mode == 1){ g_vox_enabled = true;  g_vox_clean = false; }
    else if (mode == 2){ g_vox_enabled = true;  g_vox_clean = true; tts_speak(g_eng, L" \\!wH0 ", true); }
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
    if (g_headless) dprintf("[stop] hard stop + clear queue");
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
    if (g_eng.inflight.load(std::memory_order_relaxed) == 0) {
        kick_if_idle();
    }
    return 0;
}

case WM_APP_TTS_AUDIO_DONE: {
    if (g_eng.inflight.load(std::memory_order_relaxed) == 0 && g_q.empty()) {
        gui_notify_tts_state(false);
        if (g_headless) dprintf("[tts] audio done");
    }
    return 0;
}
    }
    return DefWindowProcW(h, m, w, l);
}

// ------------------------------------------------------------------
// CLI
struct CliExtraData {
    bool* vox_enabled;
    bool* vox_clean;
    bool* selftest;
};

static bool handle_nettts_cli_option(const std::wstring& option,
                                     int&                /*index*/,
                                     int                 /*argc*/,
                                     wchar_t**           /*argv*/,
                                     AppCliContext&      ctx,
                                     void*               user_data){
    auto* extra = static_cast<CliExtraData*>(user_data);

    if (option == L"--vox"){
        if (extra && extra->vox_enabled){
            *extra->vox_enabled = true;
        }
        if (extra && extra->vox_clean){
            *extra->vox_clean = false;
        }
        return true;
    }

    if (option == L"--voxclean"){
        if (extra && extra->vox_enabled){
            *extra->vox_enabled = true;
        }
        if (extra && extra->vox_clean){
            *extra->vox_clean = true;
        }
        return true;
    }

    if (option == L"--selftest"){
        if (extra && extra->selftest){
            *extra->selftest = true;
        }
        return true;
    }

    if (option == L"--list-devices"){
        app_cli_ensure_console(ctx);
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE){
            auto put = [&](const std::wstring& s){
                DWORD written = 0;
                WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &written, nullptr);
                WriteConsoleW(h, L"\r\n", 2, &written, nullptr);
            };
            put(L"Device index mapping (use with --devnum):");
            put(L"  -1 : (Default output device)");
            UINT ndev = waveOutGetNumDevs();
            for (UINT di = 0; di < ndev; ++di){
                WAVEOUTCAPSW caps{};
                waveOutGetDevCapsW(di, &caps, sizeof(caps));
                wchar_t line[512];
                _snwprintf(line, 511, L"  %u : %ls", di, caps.szPname);
                put(line);
            }
        }
        ctx.result.exit_requested = true;
        ctx.result.exit_code      = 0;
        return true;
    }

    return false;
}

static bool on_common_timer(HWND, UINT_PTR timer_id, void*){
    if (timer_id != g_posn_timer){
        return false;
    }
    if (tts_supports_posn(g_eng)){
        DWORD p = 0;
        if (tts_posn_get(g_eng, &p)){
            dprintf("[posn] %lu", (unsigned long)p);
        }
    }
    return true;
}

static void on_common_destroy(HWND, void*){
    stop_posn_poll();
}

// ------------------------------------------------------------------
// WinMain
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int){
    app_init_common_controls();

    CliExtraData extra{&g_vox_enabled, &g_vox_clean, &g_selftest};
    AppCliHooks hooks{handle_nettts_cli_option, &extra};
    AppCliResult cli = parse_app_cli(&hooks);

    g_runserver    = cli.run_server;
    g_headless     = cli.headless;
    g_host         = cli.host;
    g_port         = cli.port;
    g_dev_index    = cli.device_index;
    g_posn_poll_ms = cli.posn_poll_ms;
    g_cli_help     = cli.help_requested;

    if (cli.has_log_path){
        log_set_path(cli.log_path);
    }
    log_set_verbose(cli.verbose_logging);

    if (cli.exit_requested){
        return cli.exit_code;
    }

    if (g_cli_help) {
        show_help_and_exit(false);
        return 0;
    }

    bool show_gui = !g_headless;

    AppWindowClass wc{};
    wc.style       = 0;
    wc.class_name  = L"NetTTS.EventHandler";
    wc.wnd_proc    = WndProc;
    wc.instance    = hInst;
    wc.cursor      = LoadCursor(nullptr, IDC_ARROW);
    wc.icon        = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP),
                                       IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.icon_small  = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP),
                                       IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    app_register_window_class(wc);

    AppWindowCreate create{};
    create.ex_style    = WS_EX_APPWINDOW;
    create.class_name  = wc.class_name;
    create.window_name = L"NetTTS Eventhandler";
    create.style       = WS_OVERLAPPEDWINDOW;
    create.x           = CW_USEDEFAULT;
    create.y           = CW_USEDEFAULT;
    create.width       = 200;
    create.height      = 200;
    create.parent      = nullptr;
    create.menu        = nullptr;
    create.instance    = hInst;
    create.param       = nullptr;

    g_hwnd = app_create_hidden_window(create);
    if (!g_hwnd){
        return 1;
    }
    ShowWindow(g_hwnd, SW_HIDE);

    HWND hDlg = nullptr;
    if (show_gui) {
        hDlg = create_main_dialog(hInst, g_hwnd);
        gui_set_app_hwnd(g_hwnd);

        if (hDlg){
            PostMessageW(hDlg, WM_APP_DEVICE_STATE, (WPARAM)g_dev_index, 0);

            auto* f = new GuiServerFields{};
            wcsncpy(f->host, g_host.c_str(), 63); f->host[63]=0;
            f->port = g_port;
            PostMessageW(hDlg, WM_APP_SET_SERVER_FIELDS, 0, (LPARAM)f);

            PostMessageW(hDlg, WM_APP_SERVER_STATE, server_is_running() ? 1 : 0, 0);

            int mode = 0;
            if (g_vox_enabled) mode = g_vox_clean ? 2 : 1;
            PostMessageW(hDlg, WM_APP_PROSODY_STATE, mode, 0);
        }
    }

    if (!tts_init(g_eng, g_dev_index)){
        MessageBeep(MB_ICONERROR);
        return 2;
    }
    tts_set_notify_hwnd(g_eng, g_hwnd);

    bool started = false;
    if (g_runserver){
        started = server_start(g_host, g_port, g_hwnd);
    }
    if (show_gui && hDlg){
        PostMessageW(hDlg, WM_APP_SERVER_STATE, started ? 1 : 0, 0);
    }

    if (g_posn_poll_ms > 0 && tts_supports_posn(g_eng)) start_posn_poll();

    if (g_selftest){
        enqueue_selftest();
        kick_if_idle();
        // literal (untagged) demo
        std::wstring literal = L"Untagged literal: \\!sf30 should be spoken literally.";
        (void)tts_speak(g_eng, literal, /*force_tagged*/false);
    }

    int pump_result = app_run_message_loop();

    tts_shutdown(g_eng);
    CoUninitialize();
    return pump_result;
}
