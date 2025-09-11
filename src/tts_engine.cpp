#include "tts_engine.hpp"
#include <cwchar>
#include <mmsystem.h>   // WAVE_MAPPER
#include "log.hpp"
#include <atomic>



#ifndef WAVE_MAPPER
#define WAVE_MAPPER ((UINT)-1)
#endif

void tts_set_rate_percent_ui(Engine& e, int pct){
    if (!e.attrsW) return;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    DWORD v = (DWORD)((pct * 255) / 100);
    (void)e.attrsW->SpeedSet(v);
}
void tts_set_pitch_percent_ui(Engine& e, int pct){
    if (!e.attrsW) return;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    DWORD v = (DWORD)((pct * 255) / 100);
    (void)e.attrsW->PitchSet(v);
}

// Instant volume still goes through ITTSAttributesW
void tts_set_volume_percent(Engine& e, int pct){
    if (!e.attrsW) return;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    DWORD v = (DWORD)((pct * 65535) / 100);
    (void)e.attrsW->VolumeSet(v);
}

// -----------------------------------------------------------
// Small debug helper (console + debugger)
static void dbg(const wchar_t* fmt, ...) {
    wchar_t wbuf[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(wbuf, 1023, fmt, ap);
    va_end(ap);
    wbuf[1023] = L'\0';
    OutputDebugStringW(wbuf);
    OutputDebugStringW(L"\n");
    char u8[2048];
    int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, u8, sizeof(u8), nullptr, nullptr);
    if (n > 0) dprintf("%s\n", u8);
}

// -----------------------------------------------------------
// Helpers
bool text_looks_tagged(const std::wstring& w){ return w.find(L"\\!") != std::wstring::npos; }

// -----------------------------------------------------------
// Notify sink: matches the 1999 speech.h (QWORD tokens)
struct BufSinkW : public ITTSBufNotifySink, public ITTSNotifySink {
    LONG    m_ref = 1;
    Engine* m_eng = nullptr;
    explicit BufSinkW(Engine* e): m_eng(e) {}

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITTSBufNotifySink)) {
            *ppv = static_cast<ITTSBufNotifySink*>(this);
        } else if (IsEqualIID(riid, IID_ITTSNotifySink) || IsEqualIID(riid, IID_ITTSNotifySinkW) || IsEqualIID(riid, IID_ITTSNotifySinkA)) {
            *ppv = static_cast<ITTSNotifySink*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)()  { return InterlockedIncrement(&m_ref); }
    STDMETHOD_(ULONG, Release)() {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }

    // ---- ITTSBufNotifySink (QWORD tokens) ----
    STDMETHOD(TextDataStarted)(QWORD /*token*/) {
        if (m_eng) {
            m_eng->inflight.fetch_add(1, std::memory_order_relaxed);
            if (m_eng->notify_hwnd) PostMessageW(m_eng->notify_hwnd, WM_APP_TTS_TEXT_START, 0, 0);
        }
        return S_OK;
    }
    STDMETHOD(TextDataDone)(QWORD /*token*/, DWORD /*hrFlags*/) {
        if (m_eng) {
            long v = m_eng->inflight.fetch_sub(1, std::memory_order_relaxed);
            if (v <= 0) m_eng->inflight.store(0, std::memory_order_relaxed);
            if (m_eng->notify_hwnd) PostMessageW(m_eng->notify_hwnd, WM_APP_TTS_TEXT_DONE, 0, 0);
        }
        return S_OK;
    }
    STDMETHOD(BookMark)(QWORD, DWORD)      { return S_OK; }
    STDMETHOD(WordPosition)(QWORD, DWORD)  { return S_OK; }

    // ---- ITTSNotifySink (QWORD tick times) ----
    STDMETHOD(AttribChanged)(DWORD)        { return S_OK; }
    STDMETHOD(AudioStart)(QWORD)           { return S_OK; }
    STDMETHOD(AudioStop)(QWORD) {
        if (m_eng) {
            dbg(L"[tts] audio stop");
            if (m_eng->notify_hwnd) PostMessageW(m_eng->notify_hwnd, WM_APP_TTS_AUDIO_DONE, 0, 0);
        }
        return S_OK;
    }
    STDMETHOD(Visual)(QWORD, WCHAR, WCHAR, DWORD, PTTSMOUTH) { return S_OK; }
};

// -----------------------------------------------------------
// Voice selection + audio binding
static bool select_voice_and_audio(Engine& e, int device_index){

    ITTSFindW* findW = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TTSEnumerator, nullptr, CLSCTX_ALL, IID_ITTSFindW, (void**)&findW);
    if (FAILED(hr) || !findW) { dbg(L"[tts] CLSID_TTSEnumerator->ITTSFindW failed hr=0x%08lx", hr); return false; }

    TTSMODEINFOW want{}; TTSMODEINFOW got{};
    hr = findW->Find(&want, nullptr, &got);
    if (FAILED(hr)) { dbg(L"[tts] ITTSFindW::Find failed hr=0x%08lx", hr); findW->Release(); return false; }

    // Always use the multimedia audio device
    IAudioMultiMediaDevice* amm = nullptr;
    hr = CoCreateInstance(CLSID_MMAudioDest, nullptr, CLSCTX_ALL, IID_IAudioMultiMediaDevice, (void**)&amm);
    if (FAILED(hr) || !amm) { dbg(L"[tts] CLSID_MMAudioDest failed"); findW->Release(); return false; }

    amm->DeviceNumSet(device_index < 0 ? (DWORD)WAVE_MAPPER : (DWORD)device_index);

    hr = findW->Select(got.gModeID, &e.cw, amm);
    findW->Release();
    if (FAILED(hr) || !e.cw) {
        dbg(L"[tts] ITTSFindW::Select failed hr=0x%08lx", hr);
        if (amm) amm->Release();
        return false;
    }

    // Keep references
    e.audio_dest = amm;

    // Attributes (optional)
    (void)e.cw->QueryInterface(IID_ITTSAttributesW, (void**)&e.attrsW);

    // Create/register notify sink (events)
    e.sink = new BufSinkW(&e);
    if (e.sink && e.cookie_evt == 0) {
        (void)e.cw->Register((PVOID)(ITTSNotifySink*)e.sink, IID_ITTSNotifySinkW, &e.cookie_evt);
    }

    // Log selected voice
    wchar_t info[512];
    _snwprintf(info, 511, L"[tts] voice: mfg=%ls product=%ls mode=%ls", got.szMfgName, got.szProductName, got.szModeName);
    dbg(L"%ls", info);

    // Probe PosnGet
    QWORD q=0; e.has_posn = SUCCEEDED(e.cw->PosnGet(&q));
    return true;
}

// -----------------------------------------------------------
// API
bool tts_init(Engine& e, int device_index){
    tts_shutdown(e);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!select_voice_and_audio(e, device_index)) return false;
    dbg(L"[tts] init: PosnGet=%s", e.has_posn ? L"yes" : L"no");
    return true;
}

void tts_shutdown(Engine& e){
    if (e.cw && e.cookie_evt) { (void)e.cw->UnRegister(e.cookie_evt); e.cookie_evt = 0; }
    if (e.sink)       { e.sink->Release();      e.sink = nullptr; }
    if (e.attrsW)     { e.attrsW->Release();    e.attrsW = nullptr; }
    if (e.cw)         { e.cw->Release();        e.cw = nullptr; }
    if (e.audio_dest) { e.audio_dest->Release(); e.audio_dest = nullptr; }
    e.inflight.store(0);
    e.notify_hwnd = nullptr;
}

HRESULT tts_speak(Engine& e, const std::wstring& wtext, bool /*force_tagged*/){
    if (!e.cw) return E_POINTER;

    SDATA s{};
    s.pData  = (PVOID)wtext.c_str();                       // WCHAR*
    s.dwSize = (DWORD)((wtext.size() + 1) * sizeof(wchar_t));

    // SAPI4 (1999): VOICECHARSET has {CHARSET_TEXT, CHARSET_IPAPHONETIC, CHARSET_ENGINEPHONETIC}.
    // On the W interface, use CHARSET_TEXT for normal text. Keep TAGGED for vendor escapes.
    const VOICECHARSET charset = CHARSET_TEXT;
    const DWORD        flags   = TTSDATAFLAG_TAGGED;

    return e.cw->TextData(charset, flags, s,
                          (PVOID)(ITTSBufNotifySink*)e.sink, IID_ITTSBufNotifySink);
}


bool tts_supports_posn(Engine& e){ return e.has_posn; }

int  tts_posn_get(Engine& e, DWORD* pos_out){
    if (!pos_out || !e.cw || !e.has_posn) return 0;
    QWORD q=0; if (FAILED(e.cw->PosnGet(&q))) return 0;
    *pos_out = (DWORD)(q & 0xFFFFFFFFu);
    return 1;
}
