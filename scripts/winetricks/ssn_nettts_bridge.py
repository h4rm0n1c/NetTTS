#!/usr/bin/env python3
import json
import re
import socket
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
from typing import Any, Dict, Optional

# ==== CONFIG =========================================================

# NetTTS TCP endpoint
NETTTS_HOST  = "127.0.0.1"
NETTTS_PORT  = 5555          # change if your NetTTS listener uses a different port

# Optional prefix tags for every spoken line
# Example: PREFIX = "/rate 0 /pitch 0 "
PREFIX       = "/rate 99 "

# Hard cap on message length so nobody can make you read a novel
MAX_LEN      = 200

# Only do TTS for these usernames (case-insensitive, from chatname)
# Leave empty set() to speak everyone. Entries are lowercased automatically.
ALLOWED_USERS = {
     "User1",
     "User2",
}

# Normalized lowercase copy for membership checks
def normalize_name(name: str) -> str:
    base = (name or "").strip().lstrip("@")
    base = re.sub(r"\s*\([^)]*\)$", "", base)  # strip trailing platform tag e.g. " (Twitch)"
    return base.lower()


ALLOWED_USERS_LOWER = {normalize_name(u) for u in ALLOWED_USERS} if ALLOWED_USERS else set()

# HTTP listener
LISTEN_HOST  = "127.0.0.1"
LISTEN_PORT  = 7878          # must match the port in &postserver=


# ==== HELPERS ========================================================

def strip_html(s: str) -> str:
    """Very simple HTML tag stripper; good enough for SSN's sanitized HTML."""
    return re.sub(r"<[^>]+>", "", s)


def send_to_nettts(text: str) -> None:
    """Send a single line of text to NetTTS over TCP."""
    line = text.replace("\n", " ").strip()
    if not line:
        return

    if len(line) > MAX_LEN:
        line = line[:MAX_LEN] + "…"

    payload = PREFIX + line + "\n"

    try:
        with socket.create_connection((NETTTS_HOST, NETTTS_PORT), timeout=2) as s:
            s.sendall(payload.encode("utf-8", errors="ignore"))
        print(f"[NetTTS] SENT: {payload.strip()}", file=sys.stderr)
    except Exception as e:
        print(f"[NetTTS] send error: {e}", file=sys.stderr)


def find_chat_payload(obj: Any) -> Optional[Dict[str, Any]]:
    """
    Recursively search for a dict that looks like a Social Stream chat payload.
    We look for keys like 'chatmessage' / 'message' / 'msg' / 'text'.
    """
    if isinstance(obj, dict):
        keys = set(obj.keys())
        if {"chatmessage", "chatname"} <= keys:
            return obj
        if ("chatmessage" in keys or "message" in keys or "msg" in keys or "text" in keys):
            return obj
        for v in obj.values():
            found = find_chat_payload(v)
            if found is not None:
                return found
    elif isinstance(obj, list):
        for item in obj:
            found = find_chat_payload(item)
            if found is not None:
                return found
    return None


def handle_event(event: Any) -> None:
    """Handle one JSON payload from Social Stream Ninja."""
    print(f"[EVENT] {event}", file=sys.stderr)

    payload = find_chat_payload(event)
    if not isinstance(payload, dict):
        return

    msg  = (
        payload.get("chatmessage")
        or payload.get("message")
        or payload.get("msg")
        or payload.get("text")
        or ""
    )
    name = payload.get("chatname") or payload.get("name") or ""
    src  = payload.get("type") or payload.get("platform") or ""

    if not msg:
        return

    # Username whitelist, if configured
    if ALLOWED_USERS_LOWER:
        name_norm = normalize_name(name)
        if name_norm not in ALLOWED_USERS_LOWER:
            print(f"[FILTER] ignoring user '{name}' (not in ALLOWED_USERS)", file=sys.stderr)
            return

    msg_plain = strip_html(msg).strip()
    if not msg_plain:
        return

    # Message only, no attribution
    text = msg_plain

    send_to_nettts(text)



# ==== HTTP SERVER ====================================================

class SSNHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0

        raw = self.rfile.read(length or 0)
        body = raw.decode("utf-8", errors="ignore")

        print(f"[HTTP] POST {self.path} len={len(body)}", file=sys.stderr)
        # Try JSON first
        try:
            data = json.loads(body)
        except Exception as e:
            print(f"[HTTP] JSON parse error: {e} body={body!r}", file=sys.stderr)
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"bad json\n")
            return

        try:
            handle_event(data)
        except Exception as e:
            print(f"[HTTP] handle_event error: {e}", file=sys.stderr)

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok\n")

    def log_message(self, fmt, *args):
        # Suppress default noisy logging; we log manually to stderr instead
        return


def run_server():
    server = HTTPServer((LISTEN_HOST, LISTEN_PORT), SSNHandler)
    print(f"[HTTP] Listening on http://{LISTEN_HOST}:{LISTEN_PORT}/ssn", file=sys.stderr)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("[HTTP] Shutting down", file=sys.stderr)


if __name__ == "__main__":
    run_server()
