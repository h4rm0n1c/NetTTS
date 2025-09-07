#pragma once
#include <windows.h>
#include <string>

bool server_start(const std::wstring& host, int port, HWND hwnd);
void server_stop();
