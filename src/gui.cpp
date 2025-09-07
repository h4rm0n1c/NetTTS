#include <windows.h>
#include "gui.hpp"
#include "help.hpp"   // for get_help_text_w()

#ifndef IDD_HELP
#define IDD_HELP       101
#define IDC_HELP_EDIT  1001
#endif

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
