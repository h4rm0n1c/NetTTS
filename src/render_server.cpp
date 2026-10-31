#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>
#include "render_server.hpp"
#include "log.hpp"

// Network input is untrusted: every read is bounded, all text lives in
// std::string with explicit size checks, parsing is strict (anything
// unexpected is rejected, never guessed at), and slow or stalled clients
// are cut off by socket timeouts.

namespace {

const int    kMaxConnections   = 8;
const size_t kMaxHeadBytes     = 8192;            // request line + headers
const size_t kMaxBodyBytes     = 3 * kRenderMaxTextBytes + 64;   // room for %-encoding
const DWORD  kRecvTimeoutMs    = 5000;
const DWORD  kSendTimeoutMs    = 30000;
const DWORD  kRenderTimeoutMs  = 60000;

HWND              g_hwnd = nullptr;
SOCKET            g_listen = INVALID_SOCKET;
HANDLE            g_thread = nullptr;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_running{false};
std::atomic<int>  g_active{0};

// ---------------------------------------------------------------- I/O

bool send_all(SOCKET s, const char* data, size_t len) {
    while (len) {
        int chunk = (int)(len > 65536 ? 65536 : len);
        int n = send(s, data, chunk, 0);
        if (n <= 0) return false;
        data += n;
        len -= (size_t)n;
    }
    return true;
}

// Reads more bytes into buf, never letting it grow past cap.
// Returns bytes read, 0 on orderly close, -1 on error/timeout or when full.
int recv_some(SOCKET s, std::string& buf, size_t cap) {
    if (buf.size() >= cap) return -1;
    char tmp[2048];
    size_t want = cap - buf.size();
    if (want > sizeof(tmp)) want = sizeof(tmp);
    int n = recv(s, tmp, (int)want, 0);
    if (n > 0) buf.append(tmp, (size_t)n);
    return n;
}

// ---------------------------------------------------------------- text helpers

bool ieq(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

enum Decode { DECODE_OK, DECODE_BAD, DECODE_TOO_LONG };

// application/x-www-form-urlencoded / query decoding, output capped at max_out.
Decode percent_decode(const std::string& in, std::string& out, size_t max_out) {
    out.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '+') c = ' ';
        else if (c == '%') {
            if (i + 2 >= in.size()) return DECODE_BAD;
            int hi = hexval(in[i + 1]), lo = hexval(in[i + 2]);
            if (hi < 0 || lo < 0) return DECODE_BAD;
            c = (char)((hi << 4) | lo);
            i += 2;
        }
        if (out.size() >= max_out) return DECODE_TOO_LONG;
        out.push_back(c);
    }
    return DECODE_OK;
}

// Value of `key` in a k=v&k=v string; found=false if absent.
Decode form_value(const std::string& form, const char* key, std::string& value, bool& found) {
    found = false;
    size_t pos = 0;
    while (pos <= form.size()) {
        size_t amp = form.find('&', pos);
        if (amp == std::string::npos) amp = form.size();
        std::string pair = form.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        std::string k, v;
        Decode d = percent_decode(pair.substr(0, eq), k, 64);
        if (d == DECODE_BAD) return d;
        if (d == DECODE_OK && k == key) {
            if (eq != std::string::npos) {
                d = percent_decode(pair.substr(eq + 1), v, kRenderMaxTextBytes);
                if (d != DECODE_OK) return d;
            }
            value = v;
            found = true;
            return DECODE_OK;
        }
        pos = amp + 1;
    }
    return DECODE_OK;
}

void decode_error(SOCKET s, Decode d, const char* where);

// Final checks on the text, whichever way it arrived: size, no NULs, valid
// UTF-8, one line (CR/LF/TAB become spaces), not empty.
bool clean_text(std::string& t) {
    if (t.size() > kRenderMaxTextBytes) return false;
    if (t.find('\0') != std::string::npos) return false;
    if (!t.empty() &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, t.data(), (int)t.size(), nullptr, 0) == 0)
        return false;
    for (char& c : t) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    t = trim(t);
    return !t.empty();
}

// ---------------------------------------------------------------- rendering

// Hands the text to the main thread and waits. Returns the HTTP-style status.
int render(const std::string& text, std::vector<BYTE>& wav) {
    auto req = std::make_shared<RenderRequest>();
    req->text_u8 = text;
    req->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!req->done) return 500;
    auto* msg = new RenderRequestPtr(req);
    if (!PostMessageW(g_hwnd, WM_APP_RENDER, 0, (LPARAM)msg)) {
        delete msg;
        return 503;
    }
    if (WaitForSingleObject(req->done, kRenderTimeoutMs) != WAIT_OBJECT_0) return 504;
    if (req->status == 200) wav.swap(req->wav);
    return req->status;
}

// ---------------------------------------------------------------- HTTP

const char* reason(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default:  return "Internal Server Error";
    }
}

void http_reply(SOCKET s, int code, const char* ctype, const char* body, size_t body_len,
                const char* extra_header = nullptr) {
    std::string head = "HTTP/1.0 " + std::to_string(code) + " " + reason(code) + "\r\n";
    head += "Content-Type: "; head += ctype; head += "\r\n";
    head += "Content-Length: " + std::to_string(body_len) + "\r\n";
    if (extra_header) { head += extra_header; head += "\r\n"; }
    head += "Connection: close\r\n\r\n";
    if (send_all(s, head.data(), head.size()) && body_len) send_all(s, body, body_len);
}

void http_error(SOCKET s, int code, const char* why) {
    std::string body = std::to_string(code) + " " + reason(code) + ": " + why + "\n";
    http_reply(s, code, "text/plain; charset=utf-8", body.data(), body.size(),
               code == 405 ? "Allow: GET, POST" : nullptr);
}

void decode_error(SOCKET s, Decode d, const char* where) {
    std::string why = (d == DECODE_TOO_LONG) ? std::string("text too long") : std::string("bad %-encoding in ") + where;
    http_error(s, d == DECODE_TOO_LONG ? 413 : 400, why.c_str());
}

const char kUsage[] =
    "NetTTS render endpoint\n"
    "  GET  /speak?text=Hello%20there      -> audio/wav\n"
    "  POST /speak  (body: text, or form field text=...)  -> audio/wav\n"
    "Raw: send one line of text, receive the WAV bytes, connection closes.\n";

// head: bytes read so far, starting with a request line that already
// looked like HTTP. Handles one request.
void handle_http(SOCKET s, std::string head) {
    size_t end;
    while ((end = head.find("\r\n\r\n")) == std::string::npos) {
        int n = recv_some(s, head, kMaxHeadBytes);
        if (n < 0 && head.size() >= kMaxHeadBytes) { http_error(s, 431, "request head too large"); return; }
        if (n <= 0) { http_error(s, n == 0 ? 400 : 408, "incomplete request"); return; }
    }
    std::string body = head.substr(end + 4);
    head.resize(end);

    // Request line: METHOD SP TARGET SP HTTP/1.x
    size_t eol = head.find("\r\n");
    std::string line = head.substr(0, eol);
    size_t sp1 = line.find(' '), sp2 = (sp1 == std::string::npos) ? sp1 : line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos || line.find(' ', sp2 + 1) != std::string::npos) {
        http_error(s, 400, "malformed request line"); return;
    }
    std::string method = line.substr(0, sp1), target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    if (target.empty() || target[0] != '/') { http_error(s, 400, "bad target"); return; }
    size_t q = target.find('?');
    std::string path = target.substr(0, q), query = (q == std::string::npos) ? "" : target.substr(q + 1);

    // Headers we care about; everything else is ignored but must be well formed.
    long long content_length = -1;
    bool expect_continue = false;
    std::string ctype;
    size_t pos = (eol == std::string::npos) ? head.size() : eol + 2;
    while (pos < head.size()) {
        size_t next = head.find("\r\n", pos);
        if (next == std::string::npos) next = head.size();
        std::string h = head.substr(pos, next - pos);
        pos = next + 2;
        size_t colon = h.find(':');
        if (colon == std::string::npos || colon == 0) { http_error(s, 400, "malformed header"); return; }
        std::string name = h.substr(0, colon), value = trim(h.substr(colon + 1));
        if (ieq(name, "content-length")) {
            if (value.empty() || value.size() > 9 || value.find_first_not_of("0123456789") != std::string::npos) {
                http_error(s, 400, "bad Content-Length"); return;
            }
            long long v = std::stoll(value);
            if (content_length >= 0 && v != content_length) { http_error(s, 400, "conflicting Content-Length"); return; }
            content_length = v;
        } else if (ieq(name, "transfer-encoding")) {
            http_error(s, 501, "Transfer-Encoding is not supported; send Content-Length"); return;
        } else if (ieq(name, "expect")) {
            if (!ieq(value, "100-continue")) { http_error(s, 400, "unsupported Expect"); return; }
            expect_continue = true;
        } else if (ieq(name, "content-type")) {
            ctype = trim(value.substr(0, value.find(';')));
        }
    }

    if (path == "/" && method == "GET") {
        http_reply(s, 200, "text/plain; charset=utf-8", kUsage, sizeof(kUsage) - 1);
        return;
    }
    if (path != "/speak") { http_error(s, 404, "use /speak"); return; }

    std::string text;
    if (method == "GET") {
        bool found = false;
        Decode d = form_value(query, "text", text, found);
        if (d != DECODE_OK) { decode_error(s, d, "query"); return; }
        if (!found) { http_error(s, 400, "missing text parameter"); return; }
    } else if (method == "POST") {
        if (content_length < 0) { http_error(s, 411, "POST needs Content-Length"); return; }
        if ((size_t)content_length > kMaxBodyBytes) { http_error(s, 413, "text too long"); return; }
        if (body.size() > (size_t)content_length) { http_error(s, 400, "body longer than Content-Length"); return; }
        if (expect_continue && body.size() < (size_t)content_length) {
            static const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
            if (!send_all(s, cont, sizeof(cont) - 1)) return;
        }
        while (body.size() < (size_t)content_length) {
            int n = recv_some(s, body, (size_t)content_length);
            if (n <= 0) { http_error(s, n == 0 ? 400 : 408, "incomplete body"); return; }
        }
        if (ieq(ctype, "application/x-www-form-urlencoded") && body.find('=') != std::string::npos) {
            bool found = false;
            Decode d = form_value(body, "text", text, found);
            if (d != DECODE_OK) { decode_error(s, d, "form"); return; }
            if (!found) { http_error(s, 400, "missing text field"); return; }
        } else {
            text = body;    // text/plain, or curl --data without a field name
        }
    } else {
        http_error(s, 405, "use GET or POST"); return;
    }

    if (text.size() > kRenderMaxTextBytes) { http_error(s, 413, "text too long"); return; }
    if (!clean_text(text)) { http_error(s, 400, "text must be non-empty UTF-8"); return; }

    std::vector<BYTE> wav;
    int code = render(text, wav);
    if (code == 200) http_reply(s, 200, "audio/wav", (const char*)wav.data(), wav.size());
    else http_error(s, code, code == 503 ? "busy, try again" : code == 504 ? "render timed out" : "render failed");
}

// ---------------------------------------------------------------- connection

bool looks_like_http(const std::string& line) {
    // "METHOD /target HTTP/1.x": a token of capitals, a space, a slash ...
    size_t sp = line.find(' ');
    if (sp == std::string::npos || sp == 0 || sp > 10) return false;
    for (size_t i = 0; i < sp; ++i) if (line[i] < 'A' || line[i] > 'Z') return false;
    if (sp + 1 >= line.size() || line[sp + 1] != '/') return false;
    size_t v = line.rfind(" HTTP/1.");
    return v != std::string::npos && v + 9 == line.size();
}

DWORD WINAPI connection_thread(LPVOID p) {
    SOCKET s = (SOCKET)(UINT_PTR)p;
    DWORD rto = kRecvTimeoutMs, sto = kSendTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rto, sizeof(rto));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sto, sizeof(sto));

    // Read the first line (bounded); it decides HTTP or raw.
    std::string buf;
    size_t nl;
    int n = 1;
    while ((nl = buf.find('\n')) == std::string::npos) {
        n = recv_some(s, buf, kMaxHeadBytes);
        if (n <= 0) break;
    }
    if (nl != std::string::npos) {
        std::string first = buf.substr(0, nl);
        if (!first.empty() && first.back() == '\r') first.pop_back();
        if (looks_like_http(first)) {
            handle_http(s, buf);
        } else {
            // Raw: the line is the text; reply with the WAV bytes or nothing.
            std::string text = first;
            if (clean_text(text)) {
                std::vector<BYTE> wav;
                if (render(text, wav) == 200) send_all(s, (const char*)wav.data(), wav.size());
            }
        }
    } else if (n == 0 && !buf.empty() && buf.size() <= kRenderMaxTextBytes) {
        // Raw client that half-closed without a newline: the bytes are the text.
        std::string text = buf;
        if (clean_text(text)) {
            std::vector<BYTE> wav;
            if (render(text, wav) == 200) send_all(s, (const char*)wav.data(), wav.size());
        }
    }
    shutdown(s, SD_SEND);
    closesocket(s);
    g_active.fetch_sub(1);
    return 0;
}

DWORD WINAPI listen_thread(LPVOID) {
    while (!g_stop.load()) {
        fd_set rf; FD_ZERO(&rf); FD_SET(g_listen, &rf);
        timeval tv{0, 200 * 1000};
        if (select(0, &rf, nullptr, nullptr, &tv) <= 0) continue;
        SOCKET s = accept(g_listen, nullptr, nullptr);
        if (s == INVALID_SOCKET) continue;
        if (g_active.load() >= kMaxConnections) {     // over the cap: drop it
            closesocket(s);
            continue;
        }
        g_active.fetch_add(1);
        HANDLE t = CreateThread(nullptr, 0, connection_thread, (LPVOID)(UINT_PTR)s, 0, nullptr);
        if (t) CloseHandle(t);
        else { closesocket(s); g_active.fetch_sub(1); }
    }
    return 0;
}

} // namespace

bool render_server_start(const std::wstring& host, int port, HWND hwnd) {
    if (g_running.load()) return true;
    if (port <= 0 || port > 65535) return false;
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;

    std::string h(host.begin(), host.end());   // host comes from local settings
    in_addr addr{};
    addr.s_addr = inet_addr(h.c_str());
    if (addr.s_addr == INADDR_NONE && h != "255.255.255.255") {
        dprintf("[render] bad host %s", h.c_str());
        return false;
    }
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) return false;
    BOOL excl = TRUE;   // nobody else may bind the same port underneath us
    setsockopt(g_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
    sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons((u_short)port); sa.sin_addr = addr;
    if (bind(g_listen, (sockaddr*)&sa, sizeof(sa)) != 0 || listen(g_listen, 16) != 0) {
        dprintf("[render] bind/listen failed on %s:%d", h.c_str(), port);
        closesocket(g_listen); g_listen = INVALID_SOCKET;
        return false;
    }
    g_hwnd = hwnd;
    g_stop.store(false);
    g_thread = CreateThread(nullptr, 0, listen_thread, nullptr, 0, nullptr);
    if (!g_thread) { closesocket(g_listen); g_listen = INVALID_SOCKET; return false; }
    g_running.store(true);
    dprintf("[render] listening on %s:%d", h.c_str(), port);
    return true;
}

void render_server_stop() {
    if (!g_running.load()) return;
    g_stop.store(true);
    if (g_thread) { WaitForSingleObject(g_thread, 2000); CloseHandle(g_thread); g_thread = nullptr; }
    if (g_listen != INVALID_SOCKET) { closesocket(g_listen); g_listen = INVALID_SOCKET; }
    g_running.store(false);
}

bool render_server_is_running() { return g_running.load(); }
