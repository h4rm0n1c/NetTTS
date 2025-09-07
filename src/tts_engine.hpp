#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <string>
#include <speech.h>   // SAPI 4 (1999 header)

// Forward decl
struct BufSinkW;

// Minimal SAPI4 engine wrapper (wide path only)
struct Engine {
    // Core SAPI
    ITTSCentralW*    cw         = nullptr;
    ITTSAttributesW* attrsW     = nullptr;

    // Notify sink we use for ITTSBufNotifySink + ITTSNotifySink(W)
    BufSinkW*        sink       = nullptr;
    DWORD            cookie_evt = 0;      // Register() cookie for ITTSNotifySink(W)

    // Bound audio destination we passed to Select()
    IUnknown*        audio_dest = nullptr;

    // Where the sink posts notifications
    HWND             notify_hwnd = nullptr;

    // Inflight counter: TextDataStarted++ / TextDataDone--
    std::atomic<long> inflight{0};

    // PosnGet availability
    bool             has_posn = false;

    // Flags/fields kept for compatibility (WAV disabled; these are ignored)
    bool             to_file   = false;
    std::wstring     wav_path;
};

// App messages (match your main.cpp; re-defining to the same value is OK)
#ifndef WM_APP
#  define WM_APP 0x8000
#endif
#define WM_APP_TTS_TEXT_START    (WM_APP + 8)
#define WM_APP_TTS_TEXT_DONE     (WM_APP + 7)

// Init / shutdown
bool tts_init   (Engine& e, int device_index /* -1 = default mapper */, bool to_file = false, const wchar_t* wav_path = nullptr);
void tts_shutdown(Engine& e);

// Where to post WM_APP_* messages
inline void tts_set_notify_hwnd(Engine& e, HWND h){ e.notify_hwnd = h; }

// Speak (we mark as tagged so \! escapes are parsed)
HRESULT tts_speak(Engine& e, const std::wstring& wtext, bool force_tagged = false);

// Engine-level controls
inline void tts_audio_reset (Engine& e){ if (e.cw) e.cw->AudioReset(); }
inline void tts_audio_pause (Engine& e){ if (e.cw) e.cw->AudioPause(); }
inline void tts_audio_resume(Engine& e){ if (e.cw) e.cw->AudioResume(); }

// PosnGet support
bool tts_supports_posn(Engine& e);
int  tts_posn_get     (Engine& e, DWORD* pos_out /* low 32 bits ok */);

// File-mode compatibility stubs (no-ops now)
inline void tts_prepare_next_file_chunk(Engine&){ /* WAV capture disabled */ }
inline void tts_file_flush(Engine&){ /* WAV capture disabled */ }

// Helper used by main.cpp
bool text_looks_tagged(const std::wstring& w);
