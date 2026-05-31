#include "util.hpp"
#include <cwctype>
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>

void rtrim(std::string &s){ while(!s.empty()&&(s.back()=='\r'||s.back()=='\n')) s.pop_back(); }

std::wstring u8_to_w(const std::string& s){
    if(s.empty()) return L"";
    int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
    std::wstring w(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&w[0],n);
    return w;
}
std::string  w_to_u8(const std::wstring& w){
    if(w.empty()) return "";
    int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);
    std::string s(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),&s[0],n,nullptr,nullptr);
    return s;
}

bool is_digits(const std::wstring& s){
    if(s.empty()) return false;
    for(wchar_t c: s){ if(!iswdigit(c)) return false; }
    return true;
}
bool is_digits_token(const std::string& s){
    if(s.empty()) return false;
    for(unsigned char c: s){ if(c<'0'||c>'9') return false; }
    return true;
}

int find_device_by_name(const std::wstring& name) {
    if (name.empty()) return -1;

    std::wstring target = name;
    std::transform(target.begin(), target.end(), target.begin(), ::towlower);

    const UINT ndev = waveOutGetNumDevs();
    for (UINT di = 0; di < ndev; ++di) {
        WAVEOUTCAPSW caps{};
        if (waveOutGetDevCapsW(di, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            std::wstring pname = caps.szPname;
            std::transform(pname.begin(), pname.end(), pname.begin(), ::towlower);

            if (pname.find(target) != std::wstring::npos) {
                return (int)di;
            }
        }
    }
    return -1;
}
