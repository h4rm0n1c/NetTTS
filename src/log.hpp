#pragma once
#include <windows.h>
#include <string>

void log_set_path(const std::wstring& path);
void log_set_verbose(bool on);   // NEW
void log_attach_console();
void log_open_default_if_needed();
bool log_has_console();
void dprintf(const char* fmt, ...);
