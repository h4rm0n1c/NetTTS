#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <speech.h>
#include <string>
#include <vector>

class WavAudioDest : public IAudio, public IAudioDest {
public:
    static HRESULT Create(const std::wstring& path, WavAudioDest** out);

    STDMETHOD(QueryInterface)(REFIID riid, LPVOID FAR* ppvObj) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(Flush)() override;
    STDMETHOD(LevelGet)(DWORD* level) override;
    STDMETHOD(LevelSet)(DWORD level) override;
    STDMETHOD(PassNotify)(PVOID sink, IID iid) override;
    STDMETHOD(PosnGet)(PQWORD pos) override;
    STDMETHOD(Claim)() override;
    STDMETHOD(UnClaim)() override;
    STDMETHOD(Start)() override;
    STDMETHOD(Stop)() override;
    STDMETHOD(TotalGet)(PQWORD total) override;
    STDMETHOD(ToFileTime)(PQWORD pos, FILETIME* ft) override;
    STDMETHOD(WaveFormatGet)(PSDATA data) override;
    STDMETHOD(WaveFormatSet)(SDATA data) override;

    STDMETHOD(FreeSpace)(DWORD* bytes, BOOL* eof) override;
    STDMETHOD(DataSet)(PVOID data, DWORD bytes) override;
    STDMETHOD(BookMark)(DWORD mark) override;

private:
    explicit WavAudioDest(HANDLE file);
    ~WavAudioDest();

    HRESULT initialize(const std::wstring& path);
    HRESULT write_all(const void* data, DWORD bytes);
    HRESULT finalize_header();

    LONG ref_count_ = 1;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::vector<BYTE> format_;
    DWORD level_ = 0x10000;
    QWORD total_bytes_ = 0;
    bool finalized_ = false;
};
