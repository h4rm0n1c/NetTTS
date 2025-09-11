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
#ifndef WM_APP_ATTRS_STATE
#define WM_APP_ATTRS_STATE (WM_APP + 22)  // main → GUI: update vol/rate/pitch sliders
#endif

// ---- POD payloads ----
struct GuiAttrs { int vol_percent; int rate_percent; int pitch_percent; };
struct GuiDeviceSel { int index; /* -1 = default */ };
struct GuiServerReq { wchar_t host[64]; int port; int start; };
struct GuiServerFields { wchar_t host[64]; int port; };
