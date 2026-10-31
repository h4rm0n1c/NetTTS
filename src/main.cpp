#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <deque>
#include <utility>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <cstdlib>
#include "log.hpp"
#include "vox_parser.hpp"
#include "tts_engine.hpp"

#include "net_server.hpp"
#include "util.hpp"   // u8_to_w()

#include "ipc.hpp"
#include "gui.hpp"   // create_main_dialog(...)
#include "help.hpp"  // usage_short(), show_help_and_exit(), print_help()
#include "wav_audio_dest.hpp"
#include "render_server.hpp"
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
static bool         g_headless_noconsole = false; // --headlessnoconsole
static std::wstring g_host          = L"127.0.0.1";
static int          g_port          = 5555;
static int          g_status_port   = -1;    // default: port+1
static int          g_render_port   = -1;    // default: port+2; 0 = off
static bool         g_status_port_explicit = false;
static int          g_dev_index     = -1;
static int          g_posn_poll_ms  = 0;
static bool         g_selftest      = false;
static std::wstring g_wav_out;      // --wav-out: one WAV file for the whole session
static std::wstring g_wav_dir;      // folder mode: one WAV file per utterance ("" = off)
static std::string  g_say_once;
static bool         g_quit_when_idle = false;

// App state
static HWND         g_hwnd          = nullptr;
static Engine       g_eng;
static LONG         g_inflight_local= 0;

static bool g_vox_enabled = false;
static bool g_vox_clean   = false; 

static bool g_cli_help  = false;  // --help (print/show help then exit)

// ------------------------------------------------------------------
// WAV output state
//
// SAPI 4 binds the engine to one audio destination when it is created, so
// "speak into a file" means re-creating the engine on a WavAudioDest and, once
// the file is done, back on the WinMM device (the same re-init a device change
// does). A request that should become a file is a "job": every chunk it is
// split into carries the job id, and its first chunk carries the file path.
// Files open and close between chunks only, while the engine is idle; the
// engine has written all of a chunk's audio by the time it reports
// TextDataDone (measured: WAV output renders faster than real time).
static bool         g_engine_wav = false; // engine currently bound to a WavAudioDest
static int          g_vol_pct    = 100;   // re-applied after every engine re-init
static int          g_next_job   = 1;
static int          g_open_job   = 0;     // job whose file is open (0 = none)
static std::wstring g_open_path;
static bool         g_open_notify_gui = false;
static RenderRequestPtr g_open_render;    // render job whose audio is being captured
static int          g_render_pending = 0; // renders accepted and not yet answered
static int          g_sent       = 0;     // chunks handed to the engine, TextDataDone pending

// Set while enqueue_incoming_text() pushes the chunks of one job.
static int          g_enqueue_job = 0;
static std::wstring g_enqueue_path;
static bool         g_enqueue_notify_gui = false;
static RenderRequestPtr g_enqueue_render;

static bool wav_mode_wanted(){ return !g_wav_out.empty() || !g_wav_dir.empty(); }

// ------------------------------------------------------------------
// Chunk queue
struct Chunk {
    std::wstring text;
    int          job = 0;           // WAV job id (0 = not part of a file job)
    std::wstring wav_path;          // set on the first chunk of a job
    bool         notify_gui = false;
    RenderRequestPtr render;        // set on the first chunk of a render job
};
static std::deque<Chunk> g_q;

static void push_chunk(std::wstring text){
    if (g_headless){
        std::string u8 = w_to_u8(text);
        dprintf("[queue] push: \"%s\"", u8.c_str());
    }
    Chunk c;
    c.text = std::move(text);
    c.job = g_enqueue_job;
    if (g_enqueue_job && !g_enqueue_path.empty()){
        c.wav_path = g_enqueue_path;
        c.notify_gui = g_enqueue_notify_gui;
        g_enqueue_path.clear();     // only the first chunk opens the file
    }
    if (g_enqueue_job && g_enqueue_render){
        c.render = std::move(g_enqueue_render);   // only the first chunk starts the capture
        g_enqueue_render.reset();
    }
    g_q.push_back(std::move(c));
}

// Speak outside the queue (prosody resets); counted like queued chunks so the
// idle logic is not fooled by their TextDataDone.
static void speak_direct(const std::wstring& w){
    if (SUCCEEDED(tts_speak(g_eng, w, true))) g_sent++;
}

// Bind the engine to WAV output or to the WinMM device. Only while idle.
static bool ensure_engine(bool want_wav){
    if (g_eng.cw && g_engine_wav == want_wav) return true;
    if (!tts_init(g_eng, g_dev_index, want_wav)){
        dprintf("[wav] engine re-init failed (wav=%d)", want_wav ? 1 : 0);
        g_engine_wav = false;
        return false;
    }
    tts_set_notify_hwnd(g_eng, g_hwnd);
    tts_set_volume_percent(g_eng, g_vol_pct);
    g_engine_wav = want_wav;
    if (want_wav && !g_wav_out.empty() && g_eng.wav){
        HRESULT hr = g_eng.wav->begin_file(g_wav_out);
        if (FAILED(hr)) dprintf("[wav] cannot open %ls hr=0x%08lx", g_wav_out.c_str(), hr);
    }
    return true;
}

static void notify_wav_result(const std::wstring& path, QWORD bytes, bool ok, bool stopped, bool to_gui){
    DWORD bps = (g_eng.wav ? g_eng.wav->avg_bytes_per_sec() : 0);
    double secs = bps ? (double)bytes / bps : 0.0;
    std::string p8 = w_to_u8(path);
    dprintf("[wav] %s %s (%.2f s)", ok ? (stopped ? "stopped" : "saved") : "FAILED", p8.c_str(), secs);
    if (ok){
        std::string line = "WAV " + p8 + "\n";
        status_server_broadcast(line.c_str(), (int)line.size());
    }
    if (HWND dlg = gui_get_main_hwnd()){
        auto* r = new GuiWavDone{};
        wcsncpy(r->path, path.c_str(), MAX_PATH - 1); r->path[MAX_PATH - 1] = 0;
        r->seconds = secs;
        r->ok = ok;
        r->stopped = stopped;
        r->from_save_button = to_gui;
        PostMessageW(dlg, WM_APP_WAV_DONE, 0, (LPARAM)r);
    }
}

// Wake the render client waiting on req (status 200 with wav, or an error).
static void answer_render(const RenderRequestPtr& req, int status){
    if (!req) return;
    req->status = status;
    if (status != 200) req->wav.clear();
    if (g_render_pending > 0) g_render_pending--;
    dprintf("[render] %d (%u bytes)", status, (unsigned)req->wav.size());
    SetEvent(req->done);
}

static void finish_open_file(bool stopped){
    if (!g_open_job) return;
    if (g_open_render){
        RenderRequestPtr req = std::move(g_open_render);
        g_open_render.reset();
        HRESULT hr = g_eng.wav ? g_eng.wav->end_buffer(req->wav) : E_FAIL;
        answer_render(req, (!stopped && hr == S_OK) ? 200 : 500);
    } else {
        QWORD bytes = 0;
        if (g_eng.wav) g_eng.wav->end_file(&bytes);
        notify_wav_result(g_open_path, bytes, true, stopped, g_open_notify_gui);
    }
    g_open_job = 0;
    g_open_path.clear();
    g_open_notify_gui = false;
}

// Drop the remaining chunks of a job that could not start.
static void drop_job(int job){
    while (!g_q.empty() && g_q.front().job == job) g_q.pop_front();
}

// A job could not start: report it the way its requester expects.
static void fail_job(const Chunk& c){
    if (c.render) answer_render(c.render, 500);
    else notify_wav_result(c.wav_path, 0, false, false, c.notify_gui);
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
static bool kick_if_idle(){
    if (g_sent > 0 || g_eng.inflight.load(std::memory_order_relaxed) > 0) return false;

    // Route the next chunk: close a finished job's file, bind the engine to
    // the right destination, open the next job's file.
    while (!g_q.empty()){
        Chunk& c = g_q.front();
        if (g_open_job && c.job != g_open_job) finish_open_file(false);
        if (!ensure_engine(wav_mode_wanted() || c.job != 0)){
            if (c.job){
                fail_job(c);
                drop_job(c.job);
                continue;
            }
            g_q.pop_front();
            continue;
        }
        if (c.job && c.job != g_open_job){
            HRESULT hr = E_FAIL;
            if (g_eng.wav && c.render) hr = g_eng.wav->begin_buffer();
            else if (g_eng.wav && !c.wav_path.empty()) hr = g_eng.wav->begin_file(c.wav_path);
            if (FAILED(hr)){
                fail_job(c);
                drop_job(c.job);
                continue;
            }
            g_open_job = c.job;
            g_open_path = c.wav_path;
            g_open_notify_gui = c.notify_gui;
            g_open_render = c.render;
        }
        break;
    }
    if (g_q.empty()) return false;

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
    if (FAILED(hr)) return false;
    g_sent++;
    return true;
}

// Nothing queued and nothing in flight.
static bool all_idle(){ return g_sent == 0 && g_q.empty(); }

// Called when the queue has fully drained.
static void on_idle(){
    if (g_open_job) finish_open_file(false);
    const bool was_wav = g_engine_wav;
    // A one-off save is finished: give the engine back to the sound device.
    if (g_engine_wav && !wav_mode_wanted()) ensure_engine(false);
    if (was_wav){
        // WAV output never raises AudioStop, so report idle here.
        gui_notify_tts_state(false);
        status_server_broadcast("STOP\n", 5);
        if (g_quit_when_idle) PostQuitMessage(0);
    }
    // Device output: --text quits on WM_APP_TTS_AUDIO_DONE, once playback ends.
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


// Lowercase words of the text joined by '_' (vendor tags and markup dropped),
// for default WAV file names: "Attention, security!" -> attention_security.
static std::wstring wav_slug(const std::string& line){
    std::wstring w = u8_to_w(line), out, word;
    size_t i = 0, n = w.size();
    auto flush = [&](){
        if (word.empty()) return;
        if (!out.empty()) out += L'_';
        out += word; word.clear();
    };
    while (i < n && out.size() < 48){
        wchar_t ch = w[i];
        if (ch == L'\\'){                       // vendor tag: skip to whitespace
            flush();
            while (i < n && !iswspace(w[i])) ++i;
            continue;
        }
        if (ch == L'[' && i + 1 < n && w[i+1] == L'['){   // [[pause N]]
            flush();
            size_t e = w.find(L"]]", i);
            i = (e == std::wstring::npos) ? n : e + 2;
            continue;
        }
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9')) word += ch;
        else if (ch >= L'A' && ch <= L'Z') word += (wchar_t)(ch - L'A' + L'a');
        else flush();
        ++i;
    }
    flush();
    if (out.size() > 48) out.resize(48);
    while (!out.empty() && out.back() == L'_') out.pop_back();
    return out.empty() ? L"speech" : out;
}

// DIR\slug.wav, or DIR\slug_2.wav ... if taken.
static std::wstring unique_wav_path(const std::wstring& dir, const std::wstring& slug){
    std::wstring base = dir;
    if (!base.empty() && base.back() != L'\\' && base.back() != L'/') base += L'\\';
    std::wstring p = base + slug + L".wav";
    for (int k = 2; GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES && k < 100000; ++k){
        wchar_t suffix[16]; _snwprintf(suffix, 15, L"_%d", k);
        p = base + slug + suffix + L".wav";
    }
    return p;
}

std::wstring nettts_wav_slug(const std::string& line){ return wav_slug(line); }

// Queue a render request: like a spoken line, but the audio is captured in
// memory for the render endpoint. Slash-commands are not interpreted (a
// remote client must not be able to /stop other speech) and the window's
// text box is left alone.
static void enqueue_render(RenderRequestPtr req){
    if (g_render_pending >= kRenderMaxQueued){
        req->status = 503;
        SetEvent(req->done);
        return;
    }
    g_render_pending++;
    g_enqueue_job = g_next_job++;
    g_enqueue_render = req;
    const std::string& line = req->text_u8;
    if (g_vox_enabled){
        std::wstring wtag = vox_process(u8_to_w(line), !g_vox_clean);
        if (!wtag.empty()) push_chunk(std::move(wtag));
    } else {
        expand_inline_pauses_and_enqueue(line);
    }
    g_enqueue_job = 0;
    if (g_enqueue_render){            // nothing to speak (e.g. only markup)
        g_enqueue_render.reset();
        answer_render(req, 500);
        return;
    }
    kick_if_idle();
}

// Enqueue one inbound line, applying --vox if enabled. wav_path (optional):
// speak this line into that file (the GUI's Save as WAV).
static void enqueue_incoming_text(const std::string& line, const std::wstring& wav_path = L"", bool notify_gui = false){
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

    // Which file (if any) this line goes to. --wav-out records the whole
    // session into one file, so it creates no per-line jobs.
    std::wstring path = wav_path;
    if (path.empty() && g_wav_out.empty() && !g_wav_dir.empty())
        path = unique_wav_path(g_wav_dir, wav_slug(line));
    if (!path.empty()){
        g_enqueue_job = g_next_job++;
        g_enqueue_path = path;
        g_enqueue_notify_gui = notify_gui;
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
    g_enqueue_job = 0;
    g_enqueue_path.clear();
    g_enqueue_notify_gui = false;
    if (HWND dlg = gui_get_main_hwnd()){
        auto* s = new std::string(line);
        PostMessageW(dlg, WM_APP_SET_TEXT, 0, (LPARAM)s);
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
        g_vol_pct = p->vol_percent;
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
            // Re-init the engine on the new device (when writing WAV files the
            // new device is used once output returns to the speakers)
            if (!g_engine_wav && all_idle()){
                if (tts_init(g_eng, g_dev_index, false)){
                    tts_set_notify_hwnd(g_eng, g_hwnd);
                    tts_set_volume_percent(g_eng, g_vol_pct);
                }
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
    if (mode == 0){ g_vox_enabled = false; g_vox_clean = false; speak_direct(L" \\!wH0 "); }
    else if (mode == 1){ g_vox_enabled = true;  g_vox_clean = false; }
    else if (mode == 2){ g_vox_enabled = true;  g_vox_clean = true; speak_direct(L" \\!wH0 "); }
    return 0;
}

case WM_APP_SERVER_REQ: {
    auto* r = (GuiServerReq*)l; // struct from gui.hpp (host/port/start)
    if (r){
        bool ok = false;
        if (r->start){
            g_host = std::wstring(r->host);
            g_port = r->port;
            if (!g_status_port_explicit){
                g_status_port = g_port + 1;
            }
            ok = server_start(g_host, g_port, g_hwnd);
            if (ok){
                ok = status_server_start(g_host, g_status_port);
            }
            if (ok && g_render_port != 0){
                int rp = g_render_port > 0 ? g_render_port : g_port + 2;
                if (!render_server_start(g_host, rp, g_hwnd)) dprintf("[render] could not start on port %d", rp);
            }
        } else {
            server_stop();
            status_server_stop();
            render_server_stop();
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

case WM_APP_RENDER: {
    auto* p = (RenderRequestPtr*)l;
    if (p){
        enqueue_render(*p);
        delete p;
    }
    return 0;
}

case WM_APP_STOP: {
    // Hard stop: clear pending queue and reset audio so current utterance halts
    for (const Chunk& c : g_q) if (c.render) answer_render(c.render, 500);
    while (!g_q.empty()) g_q.pop_front();
    tts_audio_reset(g_eng);               // immediate stop/reset (SAPI4)
    g_eng.inflight.store(0);              // best-effort local reset
    g_sent = 0;
    finish_open_file(true);               // keep what was written, report "stopped"
    if (g_engine_wav && !wav_mode_wanted()) ensure_engine(false);
    gui_notify_tts_state(false);          // reflect back to GUI
    if (g_headless) dprintf("[stop] hard stop + clear queue");
    return 0;
}

case WM_APP_SAVE_WAV: {
    auto* r = (GuiSaveWav*)l;
    if (r){
        enqueue_incoming_text(r->text_u8, r->path, true);
        delete r;
    }
    return 0;
}

case WM_APP_WAV_FOLDER: {
    auto* r = (GuiWavFolder*)l;
    if (r){
        g_wav_dir = r->enable ? std::wstring(r->dir) : std::wstring();
        dprintf("[wav] folder mode %s %ls", r->enable ? "on" : "off", r->dir);
        // Rebind now if idle so the speakers are ready again right away.
        if (all_idle() && !g_open_job) ensure_engine(wav_mode_wanted());
        delete r;
    }
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
    status_server_broadcast("START\n", 6);
    return 0;


case WM_APP_TTS_TEXT_DONE: {
    if (g_inflight_local > 0) g_inflight_local--;
    if (g_sent > 0) g_sent--;
    // Keep the open file's header current after every chunk, so a killed
    // process (or a long --wav-out session) still leaves a playable WAV.
    if (g_engine_wav && g_eng.wav) g_eng.wav->Flush();
    // (The quit check used to run here, between chunks, before the engine had
    // counted the next chunk, and cut multi-chunk --text short.)
    if (!kick_if_idle() && all_idle()) on_idle();
    return 0;
}

case WM_APP_TTS_AUDIO_DONE: {
    if (all_idle()) {
        gui_notify_tts_state(false);
        if (g_headless) dprintf("[tts] audio done");
        if (g_quit_when_idle) PostQuitMessage(0);
    }
    status_server_broadcast("STOP\n", 5);
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
        if (a==L"--help" || a == L"-h" || a == L"/?") { g_cli_help = true; }
else if (_wcsicmp(argv[i], L"--list-devices") == 0) {
    if (!g_headless) AllocConsole(); // or your own console attach
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    auto map = get_device_mapping_text();
    if (!map.empty()){
        DWORD w; WriteConsoleW(h, map.c_str(), (DWORD)map.size(), &w, nullptr);
    }
    ExitProcess(0);
}
        else if (a==L"--runserver" || a==L"--startserver") g_runserver=true;
        else if (a==L"--headless" || a==L"--verbose") g_headless=true;
        else if (a==L"--headlessnoconsole") { g_headless=true; g_headless_noconsole=true; }
        else if (a==L"--log" && i+1<argc){ log_set_path(argv[++i]); }
        else if (a==L"--wav-out" && i+1<argc){ g_wav_out = argv[++i]; g_headless = true; }
        else if (a==L"--wav-dir" && i+1<argc){ g_wav_dir = argv[++i]; }
        else if ((a==L"--text" || a==L"--say") && i+1<argc){
            std::wstring w = argv[++i];
            g_say_once = w_to_u8(w);
            g_headless = true;
            g_quit_when_idle = true;
        }
        else if (a==L"--vox"){ g_vox_enabled = true; }
        else if (a==L"--voxclean") { g_vox_enabled = true; g_vox_clean = true; }
        else if (a==L"--host" && i+1<argc) g_host = argv[++i];
        else if (a==L"--port" && i+1<argc) g_port = _wtoi(argv[++i]);
        else if (a==L"--status-port" && i+1<argc) { g_status_port = _wtoi(argv[++i]); g_status_port_explicit = true; }
        else if (a==L"--render-port" && i+1<argc) { g_render_port = _wtoi(argv[++i]); }
        else if (a==L"--devnum" && i+1<argc) {
            std::wstring dev = argv[++i];
            if (is_digits(dev)) g_dev_index = _wtoi(dev.c_str());
            else g_dev_index = find_device_by_name(dev);
        }
        else if (a==L"--posn-ms" && i+1<argc) g_posn_poll_ms = _wtoi(argv[++i]);
        else if (a==L"--selftest") g_selftest=true;
    }
    LocalFree(argv);

    if (g_status_port < 0){
        g_status_port = g_port + 1;
    }
}

// ------------------------------------------------------------------
// WinMain
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int){
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES | ICC_LINK_CLASS};
    if (!InitCommonControlsEx(&icc)){
        icc.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icc);
    }

    parse_cmdline();

    // Enumerate output devices once up front and reuse the cached mapping text.
    // Helps avoid repeated WinMM queries when showing help or responding to flags.
    (void)get_device_mapping_text();

    if (g_cli_help || (g_headless && !g_headless_noconsole)) log_attach_console();
    log_set_verbose(g_headless && !g_headless_noconsole);

    if (g_cli_help) {
        show_help_and_exit(false);
        return 0;
    }

    bool show_gui = !g_headless;

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

            if (!g_wav_dir.empty()){
                auto* f = new GuiWavFolder{};
                f->enable = true;
                wcsncpy(f->dir, g_wav_dir.c_str(), MAX_PATH - 1); f->dir[MAX_PATH - 1] = 0;
                PostMessageW(hDlg, WM_APP_WAV_FOLDER_STATE, 0, (LPARAM)f);
            }
        }
    }

    if (!g_wav_out.empty() && !g_wav_dir.empty()){
        dprintf("[wav] --wav-out and --wav-dir both given; using --wav-out (one file)");
        g_wav_dir.clear();
    }
    if (!ensure_engine(wav_mode_wanted())){
        MessageBeep(MB_ICONERROR);
        return 2;
    }
    if (!g_wav_out.empty() && (!g_eng.wav || !g_eng.wav->has_file())){
        std::string p8 = w_to_u8(g_wav_out);
        dprintf("[wav] error: cannot create %s", p8.c_str());
        MessageBeep(MB_ICONERROR);
        tts_shutdown(g_eng);
        return 3;
    }

    bool started = false;
    if (g_runserver){
        started = server_start(g_host, g_port, g_hwnd);
        if (started){
            started = status_server_start(g_host, g_status_port);
            if (!started){
                server_stop();
            }
        }
        if (started && g_render_port != 0){
            int rp = g_render_port > 0 ? g_render_port : g_port + 2;
            if (!render_server_start(g_host, rp, g_hwnd)) dprintf("[render] could not start on port %d", rp);
        }
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
        if (SUCCEEDED(tts_speak(g_eng, literal, /*force_tagged*/false))) g_sent++;
    }

    if (!g_say_once.empty()){
        enqueue_incoming_text(g_say_once);
        kick_if_idle();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0){
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    render_server_stop();
    status_server_stop();
    server_stop();
    for (const Chunk& c : g_q) if (c.render) answer_render(c.render, 503);
    finish_open_file(true);
    if (g_eng.wav) g_eng.wav->end_file();   // --wav-out session file
    tts_shutdown(g_eng);
    CoUninitialize();
    return 0;
}
