#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <speech.h>
#include <string>
#include <vector>

// SAPI 4 audio destination that writes PCM to WAV files instead of a
// WinMM device. The engine is bound to one of these at Select() time, so
// the object stays alive for the engine's lifetime while files come and go:
// begin_file() opens a new file (closing any open one), end_file() writes
// the final header and closes it. Audio that arrives with no file open is
// discarded. The engine calls DataSet() from its own thread, so every file
// operation is serialised by a critical section.
//
// Instead of a file, audio can be collected in memory (begin_buffer /
// end_buffer) for the render endpoint, which sends the WAV over the network.
class WavAudioDest : public IAudio, public IAudioDest {
public:
    // path may be empty: no file is open until begin_file().
    static HRESULT Create(const std::wstring& path, WavAudioDest** out);

    HRESULT begin_file(const std::wstring& path);
    // Finalises and closes the open file. data_bytes (optional) receives the
    // PCM byte count written to it; S_FALSE if no file was open.
    HRESULT end_file(QWORD* data_bytes = nullptr);
    bool    has_file();

    // Collect audio in memory instead of a file (closes any open file).
    HRESULT begin_buffer();
    // Stop collecting; wav receives a complete WAV image (header + PCM).
    // E_OUTOFMEMORY if the audio exceeded kMaxBufferBytes.
    HRESULT end_buffer(std::vector<BYTE>& wav);
    static const size_t kMaxBufferBytes = 64u * 1024u * 1024u;   // ~50 min at 11025 Hz
    DWORD   avg_bytes_per_sec();

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
    WavAudioDest();
    ~WavAudioDest();

    HRESULT write_all(const void* data, DWORD bytes);   // lock held
    HRESULT finalize_header();                          // lock held
    void    close_file_locked(QWORD* data_bytes);       // lock held

    LONG ref_count_ = 1;
    CRITICAL_SECTION lock_;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::vector<BYTE> format_;
    DWORD level_ = 0x10000;
    QWORD file_bytes_ = 0;      // PCM bytes in the open file
    QWORD stream_bytes_ = 0;    // all PCM bytes ever received (engine position)
    bool finalized_ = false;
    bool buffering_ = false;
    bool buffer_overflow_ = false;
    std::vector<BYTE> buffer_;
};
