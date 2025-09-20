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
- `nc`/`netcat` (optional, but required for the `speak` helper in `nettts-daemon.sh`)

By default the helper pulls the SAPI 4.0 runtime and FlexTalk voice archive directly from the canonical URLs in this repository,
so you do not need the `third_party/Dependencies` directory checked out locally. Pass alternate URLs or file paths if you keep
local mirrors.

## Usage

```bash
./scripts/winetricks/setup_nettts_prefix.sh
```

### Common options

- `--root-dir <path>` – Base directory for everything the helper creates (defaults to `~/nettts/`). The Wine prefix is stored in `/<root>/wineprefix/`.
- `--wineprefix <path>` – Target prefix to configure (useful if you already have a dedicated prefix elsewhere).
- `--wineserver <path>` – Alternate `wineserver` binary to use.
- `--wine-bin <path>` – Alternate `wine` binary to use (handy if you wrap Wine with a script such as `wrun`).
- `--nettts-url <URL>` – Override the NetTTS download location (defaults to the `v0.95c` release zip).
- `--sapi-url <URL>` – Override the SAPI runtime download (defaults to the `spchapi.exe` stored in this repo).
- `--flextalk-url <URL>` – Override the FlexTalk voice archive download (defaults to the `flextalk.zip` stored in this repo).

The script forces `WINEARCH=win32` when the prefix is first created, installs the Windows XP compatibility layer, and pulls in `vcrun6`, `mfc42`, and `riched20` via winetricks. It then downloads the SAPI 4.0 runtime and FlexTalk installers (or uses the local paths you supplied), feeds them to Wine, and finally grabs the `v0.95c` NetTTS release zip (unless overridden) before copying the extracted executable into `C:\nettts` inside the prefix. FlexTalk ships as a 1997-era InstallShield 5 package, so the helper copies the recorded `third_party/Dependencies/flextalk_setup.iss` (captured from a `/r` run under Wine) beside `setup.exe`, temporarily moves `C:\windows\system32\msvcrt40.dll` out of the way to avoid the overwrite confirmation prompt, and launches `setup.exe -s -SMS` to drive a silent install to the baked-in `C:\Program Files\Watson21` path. The original DLL is preserved as `msvcrt40.dll.nettts-backup` (and restored automatically on failure). The InstallShield log is stashed alongside the temporary payloads if you need to debug failures.

All artefacts live under the chosen root directory so they can be versioned or backed up as a single folder.

## Results

After a successful run you can expect:

- Wine prefix at `<root>/wineprefix/` (default: `~/nettts/wineprefix/`)
- NetTTS copied to `<root>/wineprefix/drive_c/nettts/`
- Attempts to create a Windows Start Menu shortcut at `<root>/wineprefix/drive_c/users/Public/Start Menu/Programs/NetTTS.lnk` (skipped with a warning if Windows Script Host is unavailable)
- Helper scripts in `<root>/bin/`:
  - `nettts-daemon.sh` – start/stop the headless TCP server, send quick lines with `speak`, and refresh the captured device list
  - `nettts-gui.sh` – launch the GUI build inside the managed prefix
  - `flextalk-controlpanel.sh` – open the FlexTalk control panel (`C:\windows\system32\flextalk.cpl`)
- Configuration and state in `<root>/etc/`:
  - `nettts-daemon.conf` – TCP host/port, VOX mode, and output device selection (VOX enabled and device `-1` by default)
  - `nettts-devices.txt` – latest snapshot from `nettts_gui.exe --list-devices`
- `/var/log/nettts.log` – log file used by the daemon helper (create and chown if your user cannot write there)

Launch NetTTS directly with `wine "$WINEPREFIX/drive_c/nettts/nettts_gui.exe"` (or swap in your `wrun` wrapper).
Re-running the script against the same prefix updates the executable in place while keeping the existing SAPI/FlexTalk setup and
refreshing the helper scripts.

### Daemon tips

- Edit `<root>/etc/nettts-daemon.conf` to change the TCP host/port, VOX mode (`vox`, `voxclean`, or `off`), or audio device index. Run `<root>/bin/nettts-daemon.sh show-config` to review the current file.
- `<root>/bin/nettts-daemon.sh list-devices` refreshes `<root>/etc/nettts-devices.txt` and prints the captured list to stdout.
- `<root>/bin/nettts-daemon.sh start` launches the server headless (`--startserver --headless`) and tails output into `/var/log/nettts.log`. Use `stop`, `restart`, `status`, or `speak "Hello world"` to manage it.
