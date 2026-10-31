#include "wav_audio_dest.hpp"
#include "log.hpp"
#include <cstring>

namespace {

struct RiffHeader {
    char riff[4];
    DWORD riff_size;
    char wave[4];
    char fmt[4];
    DWORD fmt_size;
};

struct DataHeader {
    char data[4];
    DWORD data_size;
};

WAVEFORMATEX default_format() {
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = 11025;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)((wf.nChannels * wf.wBitsPerSample) / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    wf.cbSize = 0;
    return wf;
}

DWORD format_size_from_data(SDATA data) {
    if (!data.pData || data.dwSize < sizeof(WAVEFORMATEX)) {
        return 0;
    }
    const WAVEFORMATEX* wf = static_cast<const WAVEFORMATEX*>(data.pData);
    DWORD size = sizeof(WAVEFORMATEX) + wf->cbSize;
    if (size > data.dwSize) {
        size = data.dwSize;
    }
    return size;
}

struct Lock {
    explicit Lock(CRITICAL_SECTION& cs) : cs_(cs) { EnterCriticalSection(&cs_); }
    ~Lock() { LeaveCriticalSection(&cs_); }
    CRITICAL_SECTION& cs_;
};

} // namespace

HRESULT WavAudioDest::Create(const std::wstring& path, WavAudioDest** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    WavAudioDest* dest = new WavAudioDest();
    if (!path.empty()) {
        HRESULT hr = dest->begin_file(path);
        if (FAILED(hr)) {
            dest->Release();
            return hr;
        }
    }
    *out = dest;
    return S_OK;
}

WavAudioDest::WavAudioDest() {
    InitializeCriticalSection(&lock_);
    WAVEFORMATEX wf = default_format();
    const BYTE* begin = reinterpret_cast<const BYTE*>(&wf);
    format_.assign(begin, begin + sizeof(WAVEFORMATEX));
}

WavAudioDest::~WavAudioDest() {
    {
        Lock l(lock_);
        close_file_locked(nullptr);
    }
    DeleteCriticalSection(&lock_);
}

HRESULT WavAudioDest::begin_file(const std::wstring& path) {
    Lock l(lock_);
    close_file_locked(nullptr);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        dprintf("[wav] CreateFile failed path=%ls err=%lu", path.c_str(), err);
        return HRESULT_FROM_WIN32(err);
    }
    file_ = file;
    file_bytes_ = 0;
    finalized_ = false;
    return finalize_header();   // valid (empty) WAV from the start
}

HRESULT WavAudioDest::end_file(QWORD* data_bytes) {
    Lock l(lock_);
    if (file_ == INVALID_HANDLE_VALUE) {
        if (data_bytes) *data_bytes = 0;
        return S_FALSE;
    }
    close_file_locked(data_bytes);
    return S_OK;
}

HRESULT WavAudioDest::begin_buffer() {
    Lock l(lock_);
    close_file_locked(nullptr);
    buffer_.clear();
    buffer_overflow_ = false;
    buffering_ = true;
    return S_OK;
}

HRESULT WavAudioDest::end_buffer(std::vector<BYTE>& wav) {
    Lock l(lock_);
    wav.clear();
    if (!buffering_) return S_FALSE;
    buffering_ = false;
    if (buffer_overflow_) {
        buffer_.clear();
        return E_OUTOFMEMORY;
    }
    const DWORD fmt_size = (DWORD)format_.size();
    const DWORD data_size = (DWORD)buffer_.size();
    const DWORD header_size = sizeof(RiffHeader) + fmt_size + sizeof(DataHeader);
    RiffHeader rh{{'R','I','F','F'}, header_size + data_size - 8, {'W','A','V','E'}, {'f','m','t',' '}, fmt_size};
    DataHeader dh{{'d','a','t','a'}, data_size};
    wav.reserve(header_size + data_size);
    const BYTE* p = reinterpret_cast<const BYTE*>(&rh);
    wav.insert(wav.end(), p, p + sizeof(rh));
    wav.insert(wav.end(), format_.begin(), format_.end());
    p = reinterpret_cast<const BYTE*>(&dh);
    wav.insert(wav.end(), p, p + sizeof(dh));
    wav.insert(wav.end(), buffer_.begin(), buffer_.end());
    buffer_.clear();
    buffer_.shrink_to_fit();
    return S_OK;
}

bool WavAudioDest::has_file() {
    Lock l(lock_);
    return file_ != INVALID_HANDLE_VALUE;
}

DWORD WavAudioDest::avg_bytes_per_sec() {
    Lock l(lock_);
    if (format_.size() < sizeof(WAVEFORMATEX)) return 0;
    return reinterpret_cast<const WAVEFORMATEX*>(format_.data())->nAvgBytesPerSec;
}

void WavAudioDest::close_file_locked(QWORD* data_bytes) {
    if (data_bytes) *data_bytes = file_bytes_;
    if (file_ == INVALID_HANDLE_VALUE) return;
    finalized_ = false;
    finalize_header();
    CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
    file_bytes_ = 0;
}

STDMETHODIMP WavAudioDest::QueryInterface(REFIID riid, LPVOID FAR* ppvObj) {
    if (!ppvObj) return E_POINTER;
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IAudio)) {
        *ppvObj = static_cast<IAudio*>(this);
    } else if (IsEqualIID(riid, IID_IAudioDest)) {
        *ppvObj = static_cast<IAudioDest*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) WavAudioDest::AddRef() {
    return InterlockedIncrement(&ref_count_);
}

STDMETHODIMP_(ULONG) WavAudioDest::Release() {
    ULONG count = InterlockedDecrement(&ref_count_);
    if (!count) delete this;
    return count;
}

STDMETHODIMP WavAudioDest::Flush() {
    Lock l(lock_);
    return finalize_header();
}

STDMETHODIMP WavAudioDest::LevelGet(DWORD* level) {
    if (!level) return E_POINTER;
    *level = level_;
    return S_OK;
}

STDMETHODIMP WavAudioDest::LevelSet(DWORD level) {
    level_ = level;
    return S_OK;
}

STDMETHODIMP WavAudioDest::PassNotify(PVOID, IID) {
    return S_OK;
}

STDMETHODIMP WavAudioDest::PosnGet(PQWORD pos) {
    if (!pos) return E_POINTER;
    Lock l(lock_);
    *pos = stream_bytes_;
    return S_OK;
}

STDMETHODIMP WavAudioDest::Claim() {
    return S_OK;
}

STDMETHODIMP WavAudioDest::UnClaim() {
    return S_OK;
}

STDMETHODIMP WavAudioDest::Start() {
    return S_OK;
}

STDMETHODIMP WavAudioDest::Stop() {
    Lock l(lock_);
    return finalize_header();
}

STDMETHODIMP WavAudioDest::TotalGet(PQWORD total) {
    if (!total) return E_POINTER;
    Lock l(lock_);
    *total = stream_bytes_;
    return S_OK;
}

STDMETHODIMP WavAudioDest::ToFileTime(PQWORD pos, FILETIME* ft) {
    if (!pos || !ft) return E_POINTER;
    ULARGE_INTEGER uli{};
    uli.QuadPart = *pos;
    ft->dwLowDateTime = uli.LowPart;
    ft->dwHighDateTime = uli.HighPart;
    return S_OK;
}

STDMETHODIMP WavAudioDest::WaveFormatGet(PSDATA data) {
    if (!data) return E_POINTER;
    Lock l(lock_);
    void* copy = CoTaskMemAlloc(format_.size());
    if (!copy) return E_OUTOFMEMORY;
    std::memcpy(copy, format_.data(), format_.size());
    data->pData = copy;
    data->dwSize = (DWORD)format_.size();
    return S_OK;
}

STDMETHODIMP WavAudioDest::WaveFormatSet(SDATA data) {
    DWORD size = format_size_from_data(data);
    if (!size) return E_INVALIDARG;
    Lock l(lock_);
    const BYTE* begin = static_cast<const BYTE*>(data.pData);
    format_.assign(begin, begin + size);
    finalized_ = false;
    return finalize_header();
}

STDMETHODIMP WavAudioDest::FreeSpace(DWORD* bytes, BOOL* eof) {
    if (bytes) *bytes = 0x40000000u;
    if (eof) *eof = FALSE;
    return S_OK;
}

STDMETHODIMP WavAudioDest::DataSet(PVOID data, DWORD bytes) {
    if (!data && bytes) return E_POINTER;
    Lock l(lock_);
    stream_bytes_ += bytes;
    if (buffering_) {
        if (buffer_overflow_ || buffer_.size() + bytes > kMaxBufferBytes) {
            buffer_overflow_ = true;                    // too long: fail the render
            return S_OK;
        }
        const BYTE* p = static_cast<const BYTE*>(data);
        buffer_.insert(buffer_.end(), p, p + bytes);
        return S_OK;
    }
    if (file_ == INVALID_HANDLE_VALUE) return S_OK;   // no file open: discard
    HRESULT hr = write_all(data, bytes);
    if (SUCCEEDED(hr)) {
        file_bytes_ += bytes;
        finalized_ = false;
    }
    return hr;
}

STDMETHODIMP WavAudioDest::BookMark(DWORD) {
    return S_OK;
}

HRESULT WavAudioDest::write_all(const void* data, DWORD bytes) {
    if (file_ == INVALID_HANDLE_VALUE) return E_HANDLE;
    const BYTE* p = static_cast<const BYTE*>(data);
    DWORD remaining = bytes;
    while (remaining) {
        DWORD written = 0;
        if (!WriteFile(file_, p, remaining, &written, nullptr)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        if (!written) return E_FAIL;
        p += written;
        remaining -= written;
    }
    return S_OK;
}

HRESULT WavAudioDest::finalize_header() {
    if (file_ == INVALID_HANDLE_VALUE || finalized_) return S_OK;
    if (format_.empty()) return E_INVALIDARG;

    LARGE_INTEGER pos{};
    if (!SetFilePointerEx(file_, pos, nullptr, FILE_BEGIN)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD fmt_size = (DWORD)format_.size();
    DWORD data_offset = sizeof(RiffHeader) + fmt_size + sizeof(DataHeader);
    DWORD riff_size = data_offset + (DWORD)file_bytes_ - 8;

    RiffHeader rh{{'R','I','F','F'}, riff_size, {'W','A','V','E'}, {'f','m','t',' '}, fmt_size};
    DataHeader dh{{'d','a','t','a'}, (DWORD)file_bytes_};

    HRESULT hr = write_all(&rh, sizeof(rh));
    if (FAILED(hr)) return hr;
    hr = write_all(format_.data(), fmt_size);
    if (FAILED(hr)) return hr;
    hr = write_all(&dh, sizeof(dh));
    if (FAILED(hr)) return hr;

    LARGE_INTEGER end{};
    end.QuadPart = data_offset + file_bytes_;
    if (!SetFilePointerEx(file_, end, nullptr, FILE_BEGIN)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    SetEndOfFile(file_);
    finalized_ = true;
    return S_OK;
}
