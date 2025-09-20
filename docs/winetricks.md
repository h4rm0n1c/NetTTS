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

By default the helper pulls the SAPI 4.0 runtime and FlexTalk voice archive directly from the canonical URLs in this repository,
so you do not need the `third_party/Dependencies` directory checked out locally. Pass alternate URLs or file paths if you keep
local mirrors.

## Usage

```bash
./scripts/winetricks/setup_nettts_prefix.sh
```

### Common options

- `--wineprefix <path>` – Target prefix to configure (defaults to `~/.wine-nettts` unless `WINEPREFIX` is already exported).
- `--wineserver <path>` – Alternate `wineserver` binary to use.
- `--wine-bin <path>` – Alternate `wine` binary to use (handy if you wrap Wine with a script such as `wrun`).
- `--nettts-url <URL>` – Override the NetTTS download location (defaults to the `v0.95c` release zip).
- `--sapi-url <URL>` – Override the SAPI runtime download (defaults to the `spchapi.exe` stored in this repo).
- `--flextalk-url <URL>` – Override the FlexTalk voice archive download (defaults to the `flextalk.zip` stored in this repo).

The script forces `WINEARCH=win32` when the prefix is first created, installs the Windows XP compatibility layer, and pulls in
both `vcrun6` and `mfc42` via winetricks. It then downloads the SAPI 4.0 runtime and FlexTalk installers (or uses the local
paths you supplied), launches them in sequence, and finally grabs the `v0.95c` NetTTS release zip (unless overridden) before
copying the extracted executable into `C:\nettts` inside the prefix.

During the two GUI installer steps stay nearby and click through the dialogs — the script pauses until they complete.

## Results

After a successful run you can expect:

- NetTTS copied to `$WINEPREFIX/drive_c/nettts/`
- A Windows Start Menu shortcut at `$WINEPREFIX/drive_c/users/Public/Start Menu/Programs/NetTTS.lnk`

Launch NetTTS directly with `wine start /unix "$WINEPREFIX/drive_c/nettts/nettts_gui.exe"` (or swap in your `wrun` wrapper).
Re-running the script against the same prefix updates the executable in place while keeping the existing SAPI/FlexTalk setup.
