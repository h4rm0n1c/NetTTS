#include "app_window.hpp"

AppWindowHandled handle_common_window_messages(HWND hwnd,
                                               UINT message,
                                               WPARAM wparam,
                                               LPARAM /*lparam*/,
                                               const AppWindowCallbacks* callbacks){
    AppWindowHandled handled{false, 0};

    switch (message){
    case WM_TIMER:
        if (callbacks && callbacks->on_timer){
            if (callbacks->on_timer(hwnd, static_cast<UINT_PTR>(wparam), callbacks->user_data)){
                handled.handled = true;
                handled.result  = 0;
            }
        }
        break;

    case WM_CLOSE:
        if (callbacks && callbacks->on_close){
            if (callbacks->on_close(hwnd, callbacks->user_data)){
                handled.handled = true;
                handled.result  = 0;
                break;
            }
        }
        DestroyWindow(hwnd);
        handled.handled = true;
        handled.result  = 0;
        break;

    case WM_DESTROY:
        if (callbacks && callbacks->on_destroy){
            callbacks->on_destroy(hwnd, callbacks->user_data);
        }
        PostQuitMessage(0);
        handled.handled = true;
        handled.result  = 0;
        break;

    default:
        break;
    }

    return handled;
}
