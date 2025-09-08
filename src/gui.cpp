#include <windows.h>
#include "gui.hpp"
#include "help.hpp"   // for get_help_text_w()

#ifndef IDD_HELP
#define IDD_HELP       101
#define IDC_HELP_EDIT  1001
#endif

#ifndef IDD_MAIN
#define IDD_MAIN         100
#define IDC_EDIT_TEXT    2001
#define IDC_BTN_SPEAK    2002
#define IDC_BTN_HELP     2003
#define IDC_DEV_LABEL    2100
#define IDC_DEV_COMBO    2101
#define IDC_BTN_APPLY    2102
#define IDC_VOL_LABEL    2200
#define IDC_VOL_SLIDER   2201
#define IDC_VOL_VAL      2202
#define IDC_RATE_LABEL   2210
#define IDC_RATE_SLIDER  2211
#define IDC_RATE_VAL     2212
#define IDC_PITCH_LABEL  2220
#define IDC_PITCH_SLIDER 2221
#define IDC_PITCH_VAL    2222
#define IDC_HOST_LABEL   2300
#define IDC_EDIT_HOST    2301
#define IDC_EDIT_PORT    2303
#define IDC_BTN_SERVER   2304
#define IDC_GRP_PROSODY  2403
#define IDC_RAD_CLEAN    2500
#define IDC_RAD_VOXLQ    2501
#define IDC_RAD_VOXHQ    2502
#endif

#ifndef WM_APP
# define WM_APP 0x8000
#endif
#ifndef WM_APP_SPEAK
# define WM_APP_SPEAK (WM_APP + 1)
#endif


static HWND s_mainDlg = nullptr;
static HWND s_appWnd  = nullptr;  // <- hidden app window (owner of WndProc)

HWND gui_get_main_hwnd(){ return s_mainDlg; }
void gui_set_app_hwnd(HWND h){ s_appWnd = h; }


void gui_notify_tts_state(bool busy){
    if (s_mainDlg) PostMessageW(s_mainDlg, WM_APP_TTS_STATE, busy ? 1 : 0, 0);
}


// ----------------- Main dialog -----------------
static INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam){
    static bool s_tts_busy = false;
    static bool s_server_running = false;
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE);

    switch (uMsg){
    case WM_INITDIALOG:{
        // icons omitted for brevity… keep what you already have

        HICON hi = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
        HICON si = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
        if (hi) SendMessageW(hDlg, WM_SETICON, ICON_BIG,   (LPARAM)hi);
        if (si) SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)si);

        // Default labels
        SetDlgItemTextW(hDlg, IDC_BTN_SERVER, L"Start Server");
        SetDlgItemTextW(hDlg, IDC_BTN_SPEAK,  L"Speak");

        // ---- Populate device combo from WinMM ----
        HWND hCombo = GetDlgItem(hDlg, IDC_DEV_COMBO);
        SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
        // Entry 0 = default mapper (-1)
        int idx_added = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"(Default output device)");
        SendMessageW(hCombo, CB_SETITEMDATA, idx_added, (LPARAM)-1);

        UINT ndev = waveOutGetNumDevs();
        for (UINT i=0; i<ndev; i++){
            WAVEOUTCAPSW caps{}; waveOutGetDevCapsW(i, &caps, sizeof(caps));
            idx_added = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)caps.szPname);
            SendMessageW(hCombo, CB_SETITEMDATA, idx_added, (LPARAM)i);
        }

    // Vol 0..100, Rate 30..200 (100=1.00), Pitch 50..150 (100=1.00)
    SendDlgItemMessageW(hDlg, IDC_VOL_SLIDER,  TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendDlgItemMessageW(hDlg, IDC_RATE_SLIDER, TBM_SETRANGE, TRUE, MAKELONG(30, 200));
    SendDlgItemMessageW(hDlg, IDC_PITCH_SLIDER,TBM_SETRANGE, TRUE, MAKELONG(50, 150));

    SendDlgItemMessageW(hDlg, IDC_VOL_SLIDER,  TBM_SETPOS, TRUE, 100);
    SendDlgItemMessageW(hDlg, IDC_RATE_SLIDER, TBM_SETPOS, TRUE, 100);
    SendDlgItemMessageW(hDlg, IDC_PITCH_SLIDER,TBM_SETPOS, TRUE, 100);

        // Ask main for current device index and select it
        int current = (int)SendMessageW(GetAncestor(hDlg, GA_ROOT), WM_APP_GET_DEVICE, 0, 0);
        // find item whose itemdata == current
        int count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
        for (int k=0; k<count; k++){
            int val = (int)SendMessageW(hCombo, CB_GETITEMDATA, k, 0);
            if (val == current){ SendMessageW(hCombo, CB_SETCURSEL, k, 0); break; }
        }

        SendMessageW(hDlg, WM_HSCROLL, 0, 0);

        return TRUE;
    }

    case WM_HSCROLL:{
    const HWND hVol   = GetDlgItem(hDlg, IDC_VOL_SLIDER);
    const HWND hRate  = GetDlgItem(hDlg, IDC_RATE_SLIDER);
    const HWND hPitch = GetDlgItem(hDlg, IDC_PITCH_SLIDER);

    const int vol   = (int)SendMessageW(hVol,   TBM_GETPOS, 0, 0);   // 0..100
    const int rate  = (int)SendMessageW(hRate,  TBM_GETPOS, 0, 0);   // 30..200
    const int pitch = (int)SendMessageW(hPitch, TBM_GETPOS, 0, 0);   // 50..150

    // Update the numeric labels:
    wchar_t b[32];
    _snwprintf(b, 31, L"%d%%", vol);        SetDlgItemTextW(hDlg, IDC_VOL_VAL,  b);
    _snwprintf(b, 31, L"%.2f", rate/100.0); SetDlgItemTextW(hDlg, IDC_RATE_VAL, b);
    _snwprintf(b, 31, L"%.2f", pitch/100.0);SetDlgItemTextW(hDlg, IDC_PITCH_VAL,b);

    if (s_appWnd){
        auto* p = new GuiAttrs{ vol, rate, pitch };
        PostMessageW(s_appWnd, WM_APP_ATTRS, 0, (LPARAM)p);
    }
    return TRUE;
}


case WM_APP_SET_SERVER_FIELDS: {
    auto* f = (GuiServerFields*)lParam;
    if (f){
        SetDlgItemTextW(hDlg, IDC_EDIT_HOST, f->host);
        wchar_t buf[16]; _snwprintf(buf, 15, L"%d", f->port);
        SetDlgItemTextW(hDlg, IDC_EDIT_PORT, buf);
        delete f;
    }
    return TRUE;
}

    case WM_COMMAND:{
        const WORD id = LOWORD(wParam);
        const WORD code = HIWORD(wParam);

        if (id == IDC_BTN_HELP){ show_help_dialog(hInst, hDlg); return TRUE; }

        else if ((id == IDC_RAD_CLEAN || id == IDC_RAD_VOXLQ || id == IDC_RAD_VOXHQ) && code == BN_CLICKED){
            int mode = (id==IDC_RAD_CLEAN) ? 0 : (id==IDC_RAD_VOXLQ ? 1 : 2);
            if (s_appWnd) PostMessageW(s_appWnd, WM_APP_PROSODY, mode, 0);
            return TRUE;
        }

        else if (id == IDC_BTN_SPEAK && code == BN_CLICKED){
            if (!s_tts_busy){
                // gather text, post WM_APP_SPEAK with std::string*
                int n = GetWindowTextLengthW(GetDlgItem(hDlg, IDC_EDIT_TEXT));
                if (n > 0){
                    std::wstring w(n, L'\0');
                    GetDlgItemTextW(hDlg, IDC_EDIT_TEXT, &w[0], n+1);
                    int m = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
                    std::string u8(m, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), u8.data(), m, nullptr, nullptr);
                    auto* payload = new std::string(std::move(u8));
                    if (s_appWnd) PostMessageW(s_appWnd, WM_APP_SPEAK, 0, (LPARAM)payload);
                    s_tts_busy = true;
                    SetDlgItemTextW(hDlg, IDC_BTN_SPEAK, L"Stop");
                }
            } else {
                if (s_appWnd) PostMessageW(s_appWnd, WM_APP_STOP, 0, 0);
                s_tts_busy = false;
                SetDlgItemTextW(hDlg, IDC_BTN_SPEAK, L"Speak");
            }
            return TRUE;
        }

        else if (id == IDC_BTN_APPLY && code == BN_CLICKED){
            HWND hCombo = GetDlgItem(hDlg, IDC_DEV_COMBO);
            int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0){
                int devIndex = (int)SendMessageW(hCombo, CB_GETITEMDATA, sel, 0);
                auto* payload = new GuiDeviceSel{ devIndex };
                if (s_appWnd) PostMessageW(s_appWnd, WM_APP_DEVICE, 0, (LPARAM)payload);
            }
            return TRUE;
        }

        else if (id == IDC_BTN_SERVER && code == BN_CLICKED){
            wchar_t host[64]{0}, port[16]{0};
            GetDlgItemTextW(hDlg, IDC_EDIT_HOST, host, 63);
            GetDlgItemTextW(hDlg, IDC_EDIT_PORT, port, 15);
            int p = _wtoi(port);
            auto* req = new GuiServerReq{};
            wcsncpy(req->host, host, 63); req->host[63]=0;
            req->port = p;
            req->start = s_server_running ? 0 : 1;
            if (s_appWnd) PostMessageW(s_appWnd, WM_APP_SERVER_REQ, 0, (LPARAM)req);

            return TRUE;
        }

   else if (id == IDC_DEV_COMBO && code == CBN_SELCHANGE){
        HWND hCombo = GetDlgItem(hDlg, IDC_DEV_COMBO);
        int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
        if (sel >= 0){
            int devIndex = (int)SendMessageW(hCombo, CB_GETITEMDATA, sel, 0);
            auto* p = new GuiDeviceSel{ devIndex };
            if (s_appWnd) PostMessageW(s_appWnd, WM_APP_DEVICE, 0, (LPARAM)p);
        }
        return TRUE;
    }

        else if (id == IDOK || id == IDCANCEL){
            PostQuitMessage(0);
            DestroyWindow(hDlg);
            return TRUE;
        }
        break;
    }

    // ---- app → gui notifications ----
    case WM_APP_SERVER_STATE:{
        s_server_running = (wParam != 0);
        SetDlgItemTextW(hDlg, IDC_BTN_SERVER, s_server_running ? L"Stop Server" : L"Start Server");
        return TRUE;
    }
    case WM_APP_TTS_STATE:{
        s_tts_busy = (wParam != 0);
        SetDlgItemTextW(hDlg, IDC_BTN_SPEAK, s_tts_busy ? L"Stop" : L"Speak");
        return TRUE;
    }
    case WM_APP_DEVICE_STATE:{
        HWND hCombo = GetDlgItem(hDlg, IDC_DEV_COMBO);
        int count = (int)SendMessageW(hCombo, CB_GETCOUNT, 0, 0);
        for (int k=0; k<count; k++){
            int val = (int)SendMessageW(hCombo, CB_GETITEMDATA, k, 0);
            if (val == (int)wParam){ SendMessageW(hCombo, CB_SETCURSEL, k, 0); break; }
        }
        return TRUE;
    }

    case WM_APP_PROSODY_STATE:{
        CheckDlgButton(hDlg, IDC_RAD_CLEAN,  wParam==0);
        CheckDlgButton(hDlg, IDC_RAD_VOXLQ, wParam==1);
        CheckDlgButton(hDlg, IDC_RAD_VOXHQ, wParam==2);
        return TRUE;
    }

    case WM_CLOSE:
        PostQuitMessage(0);
        DestroyWindow(hDlg);
        return TRUE;
    }
    return FALSE;
}



// Create the modeless main dialog (caller owns message pump)
HWND create_main_dialog(HINSTANCE hInst, HWND parent){
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
    HWND h = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_MAIN), parent, MainDlgProc, 0);
    if (h){
        s_mainDlg = h;                // <- remember it
        if (parent) s_appWnd = parent;      // <- talk to the hidden app window
        ShowWindow(h, SW_SHOW);
    }
    return h;
}


static INT_PTR CALLBACK HelpDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM){
    switch(uMsg){
    case WM_INITDIALOG:{

HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE);
HICON hBig   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
HICON hSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
if (hBig)   SendMessageW(hDlg, WM_SETICON, ICON_BIG,   (LPARAM)hBig);
if (hSmall) SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);

        auto txt = get_help_text_w();                 // single source of help text
        SetDlgItemTextW(hDlg, IDC_HELP_EDIT, txt.c_str());
        // avoid auto-selected text; focus OK
        HWND hEdit = GetDlgItem(hDlg, IDC_HELP_EDIT);
        if(hEdit) SendMessageW(hEdit, EM_SETSEL, 0, 0);
        SetFocus(GetDlgItem(hDlg, IDOK));
        return FALSE; // we set focus
    }
    case WM_COMMAND:
        if(LOWORD(wParam)==IDOK || LOWORD(wParam)==IDCANCEL){ DestroyWindow(hDlg); return TRUE; }
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg); return TRUE;
    }
    return FALSE;
}

bool show_help_dialog(HINSTANCE hInst, HWND parent){
    HWND h = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_HELP), parent, HelpDlgProc, 0);
    if (h){ ShowWindow(h, SW_SHOW); return true; }
    return false;
}
