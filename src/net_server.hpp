#pragma once
#include <windows.h>
#include <string>

bool server_start(const std::wstring& host, int port, HWND hwnd);
void server_stop();

bool server_is_running();  // returns true iff the TCP server is currently active

bool status_server_start(const std::wstring& host, int port);
void status_server_stop();
void status_server_broadcast(const char* msg, size_t len);
bool status_server_is_running();

