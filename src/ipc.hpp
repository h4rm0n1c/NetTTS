#pragma once
#include <windows.h>
#include <string>

// ---- Existing app messages (these guards let your old values stand) ----
#ifndef WM_APP_SPEAK
#define WM_APP_SPEAK        (WM_APP + 1)   // payload: std::string* (UTF-8)
#endif

// If you already have START/DONE etc., leave them where they are.
// #ifndef WM_APP_TTS_TEXT_START
// #define WM_APP_TTS_TEXT_START (WM_APP + 2)
// #endif
// #ifndef WM_APP_TTS_TEXT_DONE
// #define WM_APP_TTS_TEXT_DONE  (WM_APP + 3)
// #endif

// ---- New GUI→Main messages (guarded; use gaps unlikely to collide) ----
#ifndef WM_APP_ATTRS
#define WM_APP_ATTRS        (WM_APP + 10)  // sliders changed (vol/rate/pitch)
#endif
#ifndef WM_APP_DEVICE
#define WM_APP_DEVICE       (WM_APP + 11)  // apply selected output device
#endif
#ifndef WM_APP_SERVER_REQ
#define WM_APP_SERVER_REQ   (WM_APP + 12)  // start/stop server request
#endif

// ---- Main→GUI / query messages ----
#ifndef WM_APP_SERVER_STATE
#define WM_APP_SERVER_STATE (WM_APP + 13)  // wParam: 1=running, 0=stopped
#endif
#ifndef WM_APP_TTS_STATE
#define WM_APP_TTS_STATE    (WM_APP + 14)  // wParam: 1=busy, 0=idle
#endif
#ifndef WM_APP_DEVICE_STATE
#define WM_APP_DEVICE_STATE (WM_APP + 15)  // wParam: current device index
#endif
#ifndef WM_APP_GET_DEVICE
#define WM_APP_GET_DEVICE   (WM_APP + 16)  // returns LRESULT device index
#endif
#ifndef WM_APP_SET_SERVER_FIELDS
#define WM_APP_SET_SERVER_FIELDS (WM_APP + 17) // payload: GuiServerFields*
#endif

#ifndef WM_APP_STOP
#define WM_APP_STOP         (WM_APP + 18)   // GUI → main: hard stop + clear queue
#endif

#ifndef WM_APP_PROSODY
#define WM_APP_PROSODY      (WM_APP + 19)   // GUI → main: wParam: 0=clean,1=vox LQ,2=vox HQ
#endif
#ifndef WM_APP_PROSODY_STATE
#define WM_APP_PROSODY_STATE (WM_APP + 20)  // main → GUI: wParam: mode
#endif

#ifndef WM_APP_TTS_AUDIO_DONE
#define WM_APP_TTS_AUDIO_DONE (WM_APP + 21)  // engine → main: audio buffer drained
#endif

#ifndef WM_APP_RATE_STATE
#define WM_APP_RATE_STATE   (WM_APP + 22)    // main → GUI: wParam = rate pct (0..200)
#endif

#ifndef WM_APP_PITCH_STATE
#define WM_APP_PITCH_STATE  (WM_APP + 23)    // main → GUI: wParam = pitch pct (0..200)
#endif

#ifndef WM_APP_SET_TEXT
#define WM_APP_SET_TEXT     (WM_APP + 24)    // main → GUI: payload std::string*
#endif

#ifndef WM_APP_SAVE_WAV
#define WM_APP_SAVE_WAV     (WM_APP + 25)    // GUI → main: payload GuiSaveWav*
#endif
#ifndef WM_APP_WAV_FOLDER
#define WM_APP_WAV_FOLDER   (WM_APP + 26)    // GUI → main: payload GuiWavFolder*
#endif
#ifndef WM_APP_WAV_DONE
#define WM_APP_WAV_DONE     (WM_APP + 27)    // main → GUI: payload GuiWavDone*
#endif
#ifndef WM_APP_WAV_FOLDER_STATE
#define WM_APP_WAV_FOLDER_STATE (WM_APP + 28) // main → GUI: payload GuiWavFolder* (startup --wav-dir)
#endif

// ---- POD payloads ----
struct GuiAttrs { int vol_percent; int rate_percent; int pitch_percent; };
struct GuiDeviceSel { int index; /* -1 = default */ };
struct GuiServerReq { wchar_t host[64]; int port; int start; };
struct GuiServerFields { wchar_t host[64]; int port; };

// ---- WAV output payloads ----
struct GuiSaveWav   { std::wstring path; std::string text_u8; };     // speak text into path
struct GuiWavFolder { bool enable; wchar_t dir[MAX_PATH]; };          // folder mode on/off
struct GuiWavDone   { wchar_t path[MAX_PATH]; double seconds; bool ok; bool stopped; bool from_save_button; };

// Default WAV file name stem for a line of text (main.cpp): "attention_security".
std::wstring nettts_wav_slug(const std::string& line);
