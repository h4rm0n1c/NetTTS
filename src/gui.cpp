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
#endif

#ifndef WM_APP
# define WM_APP 0x8000
#endif
#ifndef WM_APP_SPEAK
# define WM_APP_SPEAK (WM_APP + 1)
#endif


// ----------------- Main dialog -----------------
static INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam){
    switch (uMsg){
    case WM_INITDIALOG:{
        // Reasonable defaults that match the code defaults
        SetDlgItemTextW(hDlg, IDC_EDIT_HOST, L"127.0.0.1");
        SetDlgItemTextW(hDlg, IDC_EDIT_PORT, L"5555");

        // Sliders: set to common baselines (values are placeholders; wiring later)
        SendDlgItemMessageW(hDlg, IDC_VOL_SLIDER,  TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendDlgItemMessageW(hDlg, IDC_VOL_SLIDER,  TBM_SETPOS,   TRUE, 100);
        SetDlgItemTextW(hDlg, IDC_VOL_VAL, L"100%");

        SendDlgItemMessageW(hDlg, IDC_RATE_SLIDER, TBM_SETRANGE, TRUE, MAKELONG(30, 200));
        SendDlgItemMessageW(hDlg, IDC_RATE_SLIDER, TBM_SETPOS,   TRUE, 100);
        SetDlgItemTextW(hDlg, IDC_RATE_VAL, L"1.00");

        SendDlgItemMessageW(hDlg, IDC_PITCH_SLIDER,TBM_SETRANGE, TRUE, MAKELONG(50, 150));
        SendDlgItemMessageW(hDlg, IDC_PITCH_SLIDER,TBM_SETPOS,   TRUE, 100);
        SetDlgItemTextW(hDlg, IDC_PITCH_VAL, L"1.00");

        return TRUE;
    }
    case WM_COMMAND:{
        const WORD id = LOWORD(wParam);
        if (id == IDC_BTN_HELP){
            show_help_dialog((HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE), hDlg);
            return TRUE;
        } else if (id == IDC_BTN_SPEAK){
            // Grab text and post it as a line to WM_APP_SPEAK.
            // This matches main.cpp’s WndProc contract (heap std::string*).
            int n = GetWindowTextLengthW(GetDlgItem(hDlg, IDC_EDIT_TEXT));
            std::wstring w(n, L'\0');
            if (n > 0){
                GetWindowTextW(GetDlgItem(hDlg, IDC_EDIT_TEXT), &w[0], n+1);
                std::string u8 = w_to_u8(w);
                auto* heap = new std::string(std::move(u8));
                PostMessageW(GetAncestor(hDlg, GA_ROOT), WM_APP_SPEAK, 0, (LPARAM)heap);
            }
            return TRUE;
        } else if (id == IDOK || id == IDCANCEL){
            DestroyWindow(hDlg);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
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
    if (h) ShowWindow(h, SW_SHOW);
    return h;
}

static INT_PTR CALLBACK HelpDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM){
    switch(uMsg){
    case WM_INITDIALOG:{
        auto txt = get_help_text_w();                 // single source of help text
        SetDlgItemTextW(hDlg, IDC_HELP_EDIT, txt.c_str());
        // avoid auto-selected text; focus OK
        HWND hEdit = GetDlgItem(hDlg, IDC_HELP_EDIT);
        if(hEdit) SendMessageW(hEdit, EM_SETSEL, 0, 0);
        SetFocus(GetDlgItem(hDlg, IDOK));
        return FALSE; // we set focus
    }
    case WM_COMMAND:
        if(LOWORD(wParam)==IDOK || LOWORD(wParam)==IDCANCEL){ EndDialog(hDlg, IDOK); return TRUE; }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDOK); return TRUE;
    }
    return FALSE;
}

bool show_help_dialog(HINSTANCE hInst, HWND parent){
    return DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_HELP), parent, HelpDlgProc, 0) == IDOK;
}
