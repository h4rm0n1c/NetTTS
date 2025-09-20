#pragma once

#include <windows.h>

struct AppWindowCallbacks {
    bool (*on_timer)(HWND hwnd, UINT_PTR timer_id, void* user_data);
    bool (*on_close)(HWND hwnd, void* user_data);
    void (*on_destroy)(HWND hwnd, void* user_data);
    void* user_data;
};

struct AppWindowHandled {
    bool    handled;
    LRESULT result;
};

AppWindowHandled handle_common_window_messages(HWND hwnd,
                                               UINT message,
                                               WPARAM wparam,
                                               LPARAM lparam,
                                               const AppWindowCallbacks* callbacks = nullptr);
