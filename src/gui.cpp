#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <new>
#include "gui.hpp"
#include "help.hpp"   // for get_help_text_w()
#include <mmsystem.h>
#include "util.hpp"   // u8_to_w

#ifndef IDD_HELP
#define IDD_HELP       101
#define IDC_HELP_EDIT      1001
#define IDC_HELP_ICON      1002
#define IDC_HELP_TITLE     1003
#define IDC_HELP_VERSION   1004
#define IDC_HELP_AUTHOR    1005
#define IDC_HELP_COPYRIGHT 1006
#define IDC_HELP_LINK      1007
#endif

#ifndef IDD_MAIN
#define IDD_MAIN         100
#define IDC_EDIT_TEXT    2001
#define IDC_BTN_SPEAK    2002
#define IDC_BTN_HELP     2003
#define IDC_BTN_SAVEWAV  2004
#define IDC_WAV_STATUS   2005
#define IDC_DEV_LABEL    2100
#define IDC_DEV_COMBO    2101
#define IDC_BTN_APPLY    2102
#define IDC_CHK_WAVDIR   2103
#define IDC_EDIT_WAVDIR  2104
#define IDC_BTN_WAVDIR   2105
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


#ifndef IDC_HAND
#define IDC_HAND MAKEINTRESOURCEW(32649)
#endif

static const wchar_t kHelpRepoUrl[] = L"https://github.com/h4rm0n1c/NetTTS/";

static HWND s_mainDlg = nullptr;
static HWND s_appWnd  = nullptr;  // <- hidden app window (owner of WndProc)

struct HelpDialogState {
    HFONT titleFont   = nullptr;
    HFONT versionFont = nullptr;
    HFONT linkFont    = nullptr;
};

HWND gui_get_main_hwnd(){ return s_mainDlg; }
void gui_set_app_hwnd(HWND h){ s_appWnd = h; }


void gui_notify_tts_state(bool busy){
    if (s_mainDlg) PostMessageW(s_mainDlg, WM_APP_TTS_STATE, busy ? 1 : 0, 0);
}


// ----------------- WAV output helpers -----------------
static std::wstring s_last_save_dir;   // folder of the last "Save as WAV..." file

static std::wstring dialog_text(HWND hDlg){
    int n = GetWindowTextLengthW(GetDlgItem(hDlg, IDC_EDIT_TEXT));
    if (n <= 0) return std::wstring();
    std::wstring w(n + 1, L'\0');
    int read = GetDlgItemTextW(hDlg, IDC_EDIT_TEXT, &w[0], (int)w.size());
    w.resize(read);
    return w;
}

static std::wstring file_name_part(const std::wstring& path){
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

static void set_wav_status(HWND hDlg, const std::wstring& text){
    SetDlgItemTextW(hDlg, IDC_WAV_STATUS, text.c_str());
}

// Device controls do nothing while speech goes to WAV files.
static void reflect_wav_folder_mode(HWND hDlg, bool on){
    EnableWindow(GetDlgItem(hDlg, IDC_DEV_COMBO), !on);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_APPLY), !on);
}

static bool browse_for_folder(HWND hDlg, std::wstring& out){
    BROWSEINFOW bi{};
    bi.hwndOwner = hDlg;
    bi.lpszTitle = L"Choose a folder for the WAV files (one file per utterance):";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) out = path;
    return ok && !out.empty();
}

static void post_wav_folder(HWND hDlg){
    bool on = IsDlgButtonChecked(hDlg, IDC_CHK_WAVDIR) == BST_CHECKED;
    auto* f = new GuiWavFolder{};
    f->enable = on;
    GetDlgItemTextW(hDlg, IDC_EDIT_WAVDIR, f->dir, MAX_PATH - 1);
    if (s_appWnd) PostMessageW(s_appWnd, WM_APP_WAV_FOLDER, 0, (LPARAM)f);
    else delete f;
    reflect_wav_folder_mode(hDlg, on);
    set_wav_status(hDlg, on ? L"Speech is saved to WAV files, not played." : L"");
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

    // Vol 0..100, Rate 0..200 (100=1.00), Pitch 0..200 (100=1.00)
    SendDlgItemMessageW(hDlg, IDC_VOL_SLIDER,  TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendDlgItemMessageW(hDlg, IDC_RATE_SLIDER, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
    SendDlgItemMessageW(hDlg, IDC_PITCH_SLIDER,TBM_SETRANGE, TRUE, MAKELONG(0, 200));

    SendDlgItemMessageW(hDlg, IDC_VOL_SLIDER,  TBM_SETPOS, TRUE, 100);
    SendDlgItemMessageW(hDlg, IDC_RATE_SLIDER, TBM_SETPOS, TRUE, 100);
    SendDlgItemMessageW(hDlg, IDC_PITCH_SLIDER,TBM_SETPOS, TRUE, 100);

        // Enumerate audio output devices immediately
        HWND hCombo = GetDlgItem(hDlg, IDC_DEV_COMBO);
        SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
        int idx_added = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"(Default output device)");
        SendMessageW(hCombo, CB_SETITEMDATA, idx_added, (LPARAM)-1);

        UINT ndev = waveOutGetNumDevs();
        for (UINT i=0; i<ndev; i++){
            WAVEOUTCAPSW caps{}; waveOutGetDevCapsW(i, &caps, sizeof(caps));
            idx_added = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)caps.szPname);
            SendMessageW(hCombo, CB_SETITEMDATA, idx_added, (LPARAM)i);
        }

        int current = (int)SendMessageW(GetAncestor(hDlg, GA_ROOT), WM_APP_GET_DEVICE, 0, 0);
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
        const int rate  = (int)SendMessageW(hRate,  TBM_GETPOS, 0, 0);   // 0..200
        const int pitch = (int)SendMessageW(hPitch, TBM_GETPOS, 0, 0);   // 0..200

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

        else if (id == IDC_BTN_SAVEWAV && code == BN_CLICKED){
            if (s_tts_busy) return TRUE;
            std::wstring w = dialog_text(hDlg);
            if (w.empty()){
                set_wav_status(hDlg, L"Type some text to save first.");
                MessageBeep(MB_ICONASTERISK);
                return TRUE;
            }
            std::string u8 = w_to_u8(w);
            wchar_t file[MAX_PATH]{};
            std::wstring def = nettts_wav_slug(u8) + L".wav";
            wcsncpy(file, def.c_str(), MAX_PATH - 1);
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            ofn.lpstrFilter = L"WAV audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"wav";
            ofn.lpstrTitle = L"Save speech as WAV";
            ofn.lpstrInitialDir = s_last_save_dir.empty() ? nullptr : s_last_save_dir.c_str();
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
            if (!GetSaveFileNameW(&ofn)) return TRUE;   // cancelled

            std::wstring path = file;
            size_t slash = path.find_last_of(L"\\/");
            if (slash != std::wstring::npos) s_last_save_dir = path.substr(0, slash);

            auto* req = new GuiSaveWav{ path, u8 };
            if (s_appWnd) PostMessageW(s_appWnd, WM_APP_SAVE_WAV, 0, (LPARAM)req);
            else { delete req; return TRUE; }
            s_tts_busy = true;
            SetDlgItemTextW(hDlg, IDC_BTN_SPEAK, L"Stop");
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_SAVEWAV), FALSE);
            set_wav_status(hDlg, L"Saving " + file_name_part(path) + L"...");
            return TRUE;
        }

        else if (id == IDC_CHK_WAVDIR && code == BN_CLICKED){
            bool on = IsDlgButtonChecked(hDlg, IDC_CHK_WAVDIR) == BST_CHECKED;
            if (on && GetWindowTextLengthW(GetDlgItem(hDlg, IDC_EDIT_WAVDIR)) == 0){
                std::wstring dir;
                if (!browse_for_folder(hDlg, dir)){
                    CheckDlgButton(hDlg, IDC_CHK_WAVDIR, BST_UNCHECKED);
                    return TRUE;
                }
                SetDlgItemTextW(hDlg, IDC_EDIT_WAVDIR, dir.c_str());
            }
            post_wav_folder(hDlg);
            return TRUE;
        }

        else if (id == IDC_BTN_WAVDIR && code == BN_CLICKED){
            std::wstring dir;
            if (browse_for_folder(hDlg, dir)){
                SetDlgItemTextW(hDlg, IDC_EDIT_WAVDIR, dir.c_str());
                if (IsDlgButtonChecked(hDlg, IDC_CHK_WAVDIR) == BST_CHECKED) post_wav_folder(hDlg);
            }
            return TRUE;
        }

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
                    std::wstring w(n + 1, L'\0');
                    int read = GetDlgItemTextW(hDlg, IDC_EDIT_TEXT, &w[0], (int)w.size());
                    w.resize(read);
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
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_SAVEWAV), !s_tts_busy);
        return TRUE;
    }
    case WM_APP_WAV_DONE:{
        auto* r = (GuiWavDone*)lParam;
        if (r){
            std::wstring name = file_name_part(r->path);
            wchar_t b[MAX_PATH + 64];
            if (!r->ok){
                _snwprintf(b, MAX_PATH + 63, L"Could not write %ls", name.c_str());
                if (r->from_save_button){
                    std::wstring msg = L"NetTTS could not write the WAV file:\r\n\r\n" + std::wstring(r->path) +
                                       L"\r\n\r\nCheck that the folder exists and is writable.";
                    MessageBoxW(hDlg, msg.c_str(), L"Save as WAV", MB_OK | MB_ICONWARNING);
                }
            } else if (r->stopped){
                _snwprintf(b, MAX_PATH + 63, L"Stopped, kept %.1f s: %ls", r->seconds, name.c_str());
            } else {
                _snwprintf(b, MAX_PATH + 63, L"Saved %.1f s: %ls", r->seconds, name.c_str());
            }
            b[MAX_PATH + 63] = 0;
            set_wav_status(hDlg, b);
            delete r;
        }
        return TRUE;
    }
    case WM_APP_WAV_FOLDER_STATE:{
        auto* f = (GuiWavFolder*)lParam;
        if (f){
            SetDlgItemTextW(hDlg, IDC_EDIT_WAVDIR, f->dir);
            CheckDlgButton(hDlg, IDC_CHK_WAVDIR, f->enable ? BST_CHECKED : BST_UNCHECKED);
            reflect_wav_folder_mode(hDlg, f->enable);
            if (f->enable) set_wav_status(hDlg, L"Speech is saved to WAV files, not played.");
            delete f;
        }
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

    case WM_APP_RATE_STATE:{
        SendDlgItemMessageW(hDlg, IDC_RATE_SLIDER, TBM_SETPOS, TRUE, (LPARAM)wParam);
        SendMessageW(hDlg, WM_HSCROLL, 0, 0);
        return TRUE;
    }
    case WM_APP_PITCH_STATE:{
        SendDlgItemMessageW(hDlg, IDC_PITCH_SLIDER, TBM_SETPOS, TRUE, (LPARAM)wParam);
        SendMessageW(hDlg, WM_HSCROLL, 0, 0);
        return TRUE;
    }
    case WM_APP_SET_TEXT:{
        std::string* s = (std::string*)lParam;
        if (s){
            auto w = u8_to_w(*s);
            SetDlgItemTextW(hDlg, IDC_EDIT_TEXT, w.c_str());
            delete s;
        }
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
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_LINK_CLASS };
    if (!InitCommonControlsEx(&icc)){
        icc.dwICC = ICC_BAR_CLASSES;
        InitCommonControlsEx(&icc);
    }
    HWND h = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_MAIN), parent, MainDlgProc, 0);
    if (h){
        s_mainDlg = h;                // <- remember it
        if (parent) s_appWnd = parent;      // <- talk to the hidden app window
        ShowWindow(h, SW_SHOW);
        UpdateWindow(h);                       // ensure initial paint
    }
    return h;
}


static INT_PTR CALLBACK HelpDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam){
    switch(uMsg){
    case WM_INITDIALOG:{
        auto* state = new(std::nothrow) HelpDialogState{};
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)state);

        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE);
        HICON hBig   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0);
        HICON hSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0);
        if (hBig)   SendMessageW(hDlg, WM_SETICON, ICON_BIG,   (LPARAM)hBig);
        if (hSmall) SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);

        HICON hHeader = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR | LR_SHARED);
        if (hHeader) SendDlgItemMessageW(hDlg, IDC_HELP_ICON, STM_SETICON, (WPARAM)hHeader, 0);

        LOGFONTW lf{};
        HFONT baseFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
        if (!baseFont || !GetObjectW(baseFont, sizeof(lf), &lf)){
            GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
        }

        if (state){
            LOGFONTW lfTitle = lf;
            lfTitle.lfWeight = FW_BOLD;
            lfTitle.lfHeight = lf.lfHeight ? MulDiv(lf.lfHeight, 3, 2) : -16;
            state->titleFont = CreateFontIndirectW(&lfTitle);
            if (state->titleFont) SendDlgItemMessageW(hDlg, IDC_HELP_TITLE, WM_SETFONT, (WPARAM)state->titleFont, TRUE);

            LOGFONTW lfVersion = lf;
            lfVersion.lfItalic = TRUE;
            state->versionFont = CreateFontIndirectW(&lfVersion);
            if (state->versionFont) SendDlgItemMessageW(hDlg, IDC_HELP_VERSION, WM_SETFONT, (WPARAM)state->versionFont, TRUE);

            LOGFONTW lfLink = lf;
            lfLink.lfUnderline = TRUE;
            lfLink.lfWeight = FW_NORMAL;
            state->linkFont = CreateFontIndirectW(&lfLink);
            if (state->linkFont) SendDlgItemMessageW(hDlg, IDC_HELP_LINK, WM_SETFONT, (WPARAM)state->linkFont, TRUE);
        }

        SetDlgItemTextW(hDlg, IDC_HELP_LINK, kHelpRepoUrl);

        auto txt = get_help_text_w();                 // single source of help text
        SetDlgItemTextW(hDlg, IDC_HELP_EDIT, txt.c_str());
        // avoid auto-selected text; focus OK
        HWND hEdit = GetDlgItem(hDlg, IDC_HELP_EDIT);
        if(hEdit) SendMessageW(hEdit, EM_SETSEL, 0, 0);
        SetFocus(GetDlgItem(hDlg, IDOK));
        return FALSE; // we set focus
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_HELP_LINK && (HIWORD(wParam) == STN_CLICKED || HIWORD(wParam) == STN_DBLCLK)){
            ShellExecuteW(hDlg, L"open", kHelpRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if(LOWORD(wParam)==IDOK || LOWORD(wParam)==IDCANCEL){ DestroyWindow(hDlg); return TRUE; }
        break;
    case WM_NOTIFY:{
        auto* hdr = reinterpret_cast<LPNMHDR>(lParam);
        if (hdr && hdr->idFrom == IDC_HELP_LINK && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)){
            auto* link = reinterpret_cast<PNMLINK>(lParam);
            const wchar_t* url = (link && link->item.szUrl[0]) ? link->item.szUrl : kHelpRepoUrl;
            ShellExecuteW(hDlg, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hDlg); return TRUE;
    case WM_SETCURSOR:{
        if ((HWND)wParam == GetDlgItem(hDlg, IDC_HELP_LINK)){
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:{
        HDC hdc = (HDC)wParam;
        HWND hwndCtl = (HWND)lParam;
        if (GetDlgCtrlID(hwndCtl) == IDC_HELP_LINK){
            SetTextColor(hdc, RGB(0, 102, 204));
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    }
    case WM_DESTROY:{
        auto* state = reinterpret_cast<HelpDialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
        if (state){
            if (state->titleFont) DeleteObject(state->titleFont);
            if (state->versionFont) DeleteObject(state->versionFont);
            if (state->linkFont) DeleteObject(state->linkFont);
            delete state;
            SetWindowLongPtrW(hDlg, GWLP_USERDATA, 0);
        }
        break;
    }
    }
    return FALSE;
}

bool show_help_dialog(HINSTANCE hInst, HWND parent){
    HWND h = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_HELP), parent, HelpDlgProc, 0);
    if (h){ ShowWindow(h, SW_SHOW); return true; }
    return false;
}
