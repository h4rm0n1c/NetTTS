# Wine prefix bootstrap (winetricks)

The repository ships everything needed to stand up a dedicated Wine prefix for NetTTS and the bundled FlexTalk voice.
The `scripts/winetricks/setup_nettts_prefix.sh` helper automates the moving pieces so you can go from a fresh prefix to a
ready-to-launch installation in a single command.

## Prerequisites

Install these host tools before running the helper:

- `wine` 8.0 or newer (32-bit support enabled)
- `winetricks`
- `curl`
- `unzip`

The helper expects to be launched from the repository root so it can reach the vendored SAPI 4.0 runtime and FlexTalk installer.

## Usage

```bash
./scripts/winetricks/setup_nettts_prefix.sh \
  --nettts-url "https://github.com/<owner>/NetTTS/releases/download/<tag>/nettts_gui.exe"
```

### Common options

- `--wineprefix <path>` – Target prefix to configure (defaults to `~/.wine-nettts` unless `WINEPREFIX` is already exported).
- `--wineserver <path>` – Alternate `wineserver` binary to use.
- `--wine-bin <path>` – Alternate `wine` binary to use (handy if you wrap Wine with a script such as `wrun`).
- `--nettts-url <URL>` – Required. Download location for the NetTTS executable you want to install into the prefix.

The script forces `WINEARCH=win32` when the prefix is first created, installs the Windows XP compatibility layer, and pulls in
both `vcrun6` and `mfc42` via winetricks. It then launches the SAPI 4.0 runtime installer, unpacks FlexTalk to a temporary
folder, runs its `SETUP.EXE`, and finally downloads the requested NetTTS build into `C:\nettts` inside the prefix.

During the two GUI installer steps stay nearby and click through the dialogs — the script pauses until they complete.

## Results

After a successful run you can expect:

- NetTTS copied to `$WINEPREFIX/drive_c/nettts/`
- A Windows Start Menu shortcut at `$WINEPREFIX/drive_c/users/Public/Start Menu/Programs/NetTTS.lnk`

Launch NetTTS directly with `wine start /unix "$WINEPREFIX/drive_c/nettts/nettts_gui.exe"` (or swap in your `wrun` wrapper).
Re-running the script against the same prefix updates the executable in place while keeping the existing SAPI/FlexTalk setup.
