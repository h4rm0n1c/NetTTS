#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include "util.hpp"

// just the dialog for now — we'll add more later as we bring the GUI back
bool show_help_dialog(HINSTANCE hInst, HWND parent);
HWND create_main_dialog(HINSTANCE hInst, HWND parent = nullptr);
