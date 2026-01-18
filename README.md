# NetTTS

<p align="center">
  <img src="app.ico" alt="NetTTS icon" width="96" />
</p>

<p align="center">
  <img src="nettts_main_window.png" alt="NetTTS main window" width="720" />
</p>


NetTTS is a small Win32 GUI that wraps FlexTalk and other SAPI 4.0 voices. It’s meant to run on old Windows boxes, modern Windows, or Wine.

## Why it exists

- Works on retro Windows installs without a hunt for old DLLs.
- Builds cleanly on Linux with MinGW-w64.
- Bundles a known-good `speech.h` so headers don’t drift.
- Matches the maintainer’s Devuan + Wine workflow.

## Run the prebuilt binary

1. Download the zip from [Releases](../../releases) and unpack it.
2. Install the SAPI 4.0 runtime (`third_party/Dependencies/spchapi.exe`) if needed.
3. Install FlexTalk from `third_party/Dependencies/flextalk.zip`.
4. Launch `nettts_gui.exe` on Windows or via Wine (`wrun ./build/nettts_gui.exe`).

Want to make it sing? There’s a [NetTTS sing-along gist](https://gist.github.com/h4rm0n1c/2ddaa14c03be25c2072347a1b27e25da).

## Wine prefix automation

If you want a ready-to-roll Wine XP sandbox with SAPI 4.0, FlexTalk, and NetTTS preinstalled, run:

```bash
./scripts/winetricks/setup_nettts_prefix.sh
```

Or pull the latest helper directly from GitHub:

```bash
curl -fsSLo setup_nettts_prefix.sh https://raw.githubusercontent.com/h4rm0n1c/NetTTS/main/scripts/winetricks/setup_nettts_prefix.sh \
  && bash setup_nettts_prefix.sh --root-dir "$(pwd)/nettts"
```

Defaults:

- Prefix and files land under `~/nettts/`.
- Prefix path: `~/nettts/wineprefix/`.
- Logs: `~/nettts/wineprefix/drive_c/nettts/nettts.log`.

The script uses winetricks (`winxp`, `vcrun6`, `mfc42`, `riched20`), downloads SAPI, FlexTalk, and the NetTTS zip, then wires up helper scripts. FlexTalk’s installer still runs interactively.

Helper scripts:

- `~/nettts/bin/nettts-daemon.sh` – start/stop the TCP server.
- `~/nettts/bin/nettts-gui.sh` – launch the GUI.
- `~/nettts/bin/flextalk-controlpanel.sh` – open the FlexTalk control panel.

More detail: [docs/winetricks.md](docs/winetricks.md).

## Quick start build (Linux host)

```bash
sudo apt-get update
sudo apt-get install -y make mingw-w64 g++-mingw-w64-i686

make -f Makefile.mingw -j"$(nproc)"
# → build/nettts_gui.exe
```

The binary lands in `./build/` and can be run via Wine (`$HOME/bin/wrun ./build/nettts_gui.exe`).

## Customize the include path

By default the build uses `third_party/include/speech.h`. To point at another SDK:

```bash
make -f Makefile.mingw INC_DIR="C:/Program Files/Microsoft Speech SDK/Include" -j"$(nproc)"
```

## Housekeeping

- Clean: `make -f Makefile.mingw clean`
- Artifacts: `./build/`
- Optional: `third_party/Dependencies/` may contain installers for SAPI/FlexTalk.

## Live status sidechannel

Run `nettts_gui.exe --runserver` (or press **Start server**) to expose:

- Command socket on `--port` (default `5555`).
- Status socket on `--status-port` (defaults to `--port+1`, so `5556`).

Status socket messages:

- `START` when speech begins.
- `STOP` when playback ends.

### One-shot TCP commands

The command socket keeps the connection open. To send one line and exit:

```bash
printf 'Hello World From Net TTS NetCat TCP Server!\n' | nc -q 0 127.0.0.1 5555
```

BSD netcat also supports `-N`:

```bash
printf 'Message for Gordon Freeman, you will not escape this time.\n' | nc -N 127.0.0.1 5555
```

A minimal status consumer:

```bash
nc 127.0.0.1 5556 | while read -r line; do
  case "$line" in
    START) echo "duck bgm" ;;   # replace with your OBS control shim
    STOP)  echo "restore" ;;
  esac
done
```

Thanks to Valve for the games and to the classic Win32 UI era that inspired this project.
