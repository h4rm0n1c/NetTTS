#include "app_bootstrap.hpp"

#include <commctrl.h>

bool app_init_common_controls(){
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_LINK_CLASS;
    if (InitCommonControlsEx(&icc)){
        return true;
    }
    icc.dwICC = ICC_WIN95_CLASSES;
    return InitCommonControlsEx(&icc) != FALSE;
}

ATOM app_register_window_class(const AppWindowClass& info){
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = info.style;
    wc.lpfnWndProc   = info.wnd_proc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = info.instance;
    wc.hIcon         = info.icon;
    wc.hCursor       = info.cursor ? info.cursor : LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = info.class_name;
    wc.hIconSm       = info.icon_small;
    return RegisterClassExW(&wc);
}

HWND app_create_hidden_window(const AppWindowCreate& info){
    return CreateWindowExW(info.ex_style,
                           info.class_name,
                           info.window_name,
                           info.style,
                           info.x,
                           info.y,
                           info.width,
                           info.height,
                           info.parent,
                           info.menu,
                           info.instance,
                           info.param);
}

int app_run_message_loop(){
    MSG msg;
    while (true){
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0){
            return static_cast<int>(msg.wParam);
        }
        if (r == -1){
            return -1;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
