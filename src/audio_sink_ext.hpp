#pragma once
struct IAudioSinkExt {
    virtual ~IAudioSinkExt() {}
    virtual void insert_silence_ms(unsigned ms) = 0;   // write zeros at current position
    virtual void close_sink() = 0;                      // finalize headers, flush, etc.
};
