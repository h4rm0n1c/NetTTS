#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

// Render endpoint: text in, WAV bytes out, on its own port.
//
// One port serves two kinds of client. If the first line is an HTTP request
// line, the request is handled as HTTP (GET /speak?text=..., POST /speak);
// otherwise the first line is the text and the reply is the raw WAV bytes,
// after which the connection closes (netcat style).
//
// Each connection gets its own thread. It hands a RenderRequest to the main
// thread (WM_APP_RENDER) and waits for it; the main thread renders through
// the normal speech queue and signals `done`. The request is reference
// counted so a client that gives up early cannot leave the main thread
// writing into freed memory.
struct RenderRequest {
    std::string        text_u8;          // validated UTF-8, one line, <= kRenderMaxTextBytes
    HANDLE             done = nullptr;   // manual-reset event, set by the main thread
    int                status = 500;     // 200 ok, 503 busy, 500 render failed
    std::vector<BYTE>  wav;              // complete WAV image when status == 200
    ~RenderRequest() { if (done) CloseHandle(done); }
};
using RenderRequestPtr = std::shared_ptr<RenderRequest>;

#ifndef WM_APP_RENDER
#define WM_APP_RENDER (WM_APP + 29)   // render thread -> main: lParam = new RenderRequestPtr (receiver deletes)
#endif

const size_t kRenderMaxTextBytes = 4096;   // UTF-8 bytes of text per request
const int    kRenderMaxQueued    = 8;      // renders waiting or in progress

bool render_server_start(const std::wstring& host, int port, HWND hwnd);
void render_server_stop();
bool render_server_is_running();
