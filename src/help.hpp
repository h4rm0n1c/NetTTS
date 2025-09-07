#pragma once
#include <string>
#include <windows.h>
#include "log.hpp"

void usage_short();
void print_help();
void show_help_and_exit(bool error=false);
bool show_help_dialog(HINSTANCE hInst, HWND parent);
std::wstring get_help_text_w();
