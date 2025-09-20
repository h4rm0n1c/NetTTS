#pragma once

#include <windows.h>

struct AppWindowClass {
    UINT     style;
    LPCWSTR  class_name;
    WNDPROC  wnd_proc;
    HINSTANCE instance;
    HCURSOR  cursor;
    HICON    icon;
    HICON    icon_small;
};

struct AppWindowCreate {
    DWORD    ex_style;
    LPCWSTR  class_name;
    LPCWSTR  window_name;
    DWORD    style;
    int      x;
    int      y;
    int      width;
    int      height;
    HWND     parent;
    HMENU    menu;
    HINSTANCE instance;
    LPVOID   param;
};

bool app_init_common_controls();

ATOM app_register_window_class(const AppWindowClass& info);

HWND app_create_hidden_window(const AppWindowCreate& info);

int app_run_message_loop();
