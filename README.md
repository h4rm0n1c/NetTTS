# NetTTS — Build Instructions (MinGW-w64 i686)

This repository builds a Win32 executable using MinGW-w64 (32‑bit). The build does **not** require a system‑installed SAPI 4 SDK because a known‑good header is included in the tree.

---

## Prerequisites (Linux host)

- `make`
- `mingw-w64` 32‑bit cross compiler: `i686-w64-mingw32-g++`
- Resource compiler: `i686-w64-mingw32-windres`

Devuan/Ubuntu/Debian example:

```bash
sudo apt-get update
sudo apt-get install -y make mingw-w64 g++-mingw-w64-i686
```

---

## Build

From the repo root:

```bash
make -f Makefile.mingw -j"$(nproc)"
# → build/nettts_gui.exe
```

### Include path

The Makefile defaults to the committed header at:

```
third_party/include/speech.h
```

If you prefer a different SDK location, you can override the include path:

```bash
make -f Makefile.mingw INC_DIR="C:/Program Files/Microsoft Speech SDK/Include" -j"$(nproc)"
```

Clean:

```bash
make -f Makefile.mingw clean
```

---

## Notes

- **Output directory:** all artifacts are placed in `./build/`.
- **Local headers:** SAPI 4.0 `speech.h` is included at `third_party/include/` for reproducible builds.
- **Dependencies folder:** The `Dependencies/` directory (if present) may contain installer EXEs for the **SAPI 4.0 SDK**, **SAPI 4.0 runtime**, and **FlexTalk** voice. These are **optional** for building (thanks to the included header) but can be used to install the SDK/runtime/engine on Windows or under Wine if needed.
