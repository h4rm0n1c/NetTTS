#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include "util.hpp"
#include <mmsystem.h>   // waveOut*
#pragma comment(lib, "winmm.lib")
#include "ipc.hpp" 

#ifndef IDI_APP
#define IDI_APP  1
#endif

// just the dialog for now — we'll add more later as we bring the GUI back
bool show_help_dialog(HINSTANCE hInst, HWND parent);
HWND create_main_dialog(HINSTANCE hInst, HWND parent = nullptr);

// ---- GUI helpers (implemented in gui.cpp) ----
HWND gui_get_main_hwnd();            // returns the dialog HWND if created, else nullptr
bool gui_notify_tts_state(bool busy); // returns true if the WM_APP_TTS_STATE post succeeded
void gui_set_app_hwnd(HWND hwnd);
