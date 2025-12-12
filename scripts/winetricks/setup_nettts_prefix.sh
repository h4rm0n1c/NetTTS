#!/usr/bin/env bash
set -euo pipefail

unset LD_PRELOAD LD_LIBRARY_PATH GTK3_MODULES GTK_MODULES GTK_PATH QT_PLUGIN_PATH QT_QPA_PLATFORMTHEME || true

DEFAULT_NETTTS_URL="https://github.com/h4rm0n1c/NetTTS/releases/download/v0.95c/nettts_gui.zip"
DEFAULT_SAPI_URL="https://github.com/h4rm0n1c/NetTTS/raw/refs/heads/main/third_party/Dependencies/spchapi.exe"
DEFAULT_FLEXTALK_URL="https://github.com/h4rm0n1c/NetTTS/raw/refs/heads/main/third_party/Dependencies/flextalk.zip"
DEFAULT_SSN_BRIDGE_REF="main"
DEFAULT_SSN_BRIDGE_URL="https://raw.githubusercontent.com/h4rm0n1c/NetTTS/${DEFAULT_SSN_BRIDGE_REF}/scripts/winetricks/ssn_nettts_bridge.py"
DEFAULT_HELPER_REF="main"
DEFAULT_DAEMON_URL="https://raw.githubusercontent.com/h4rm0n1c/NetTTS/${DEFAULT_HELPER_REF}/scripts/winetricks/nettts-daemon.sh"
DEFAULT_GUI_URL="https://raw.githubusercontent.com/h4rm0n1c/NetTTS/${DEFAULT_HELPER_REF}/scripts/winetricks/nettts-gui.sh"
DEFAULT_FLEXCTL_URL="https://raw.githubusercontent.com/h4rm0n1c/NetTTS/${DEFAULT_HELPER_REF}/scripts/winetricks/flextalk-controlpanel.sh"
DEFAULT_ROOT_DIR="$HOME/nettts"

usage() {
        cat <<USAGE
Usage: setup_nettts_prefix.sh [options]

Prepare a Wine prefix with SAPI 4.0, FlexTalk, and NetTTS installed.

Options:
  --root-dir <path>     Base directory for NetTTS assets (default: $DEFAULT_ROOT_DIR)
  --wineprefix <path>   Target Wine prefix (default: <root>/wineprefix or \$WINEPREFIX if set)
  --wineserver <path>   Override the wineserver binary (default: \$WINESERVER or "wineserver")
  --wine-bin <path>     Override the wine binary (default: \$WINE or "wine")
  --nettts-url <URL>    Override the NetTTS download (default: $DEFAULT_NETTTS_URL)
  --sapi-url <URL>      Override the SAPI runtime download (default: $DEFAULT_SAPI_URL)
  --flextalk-url <URL>  Override the FlexTalk archive download (default: $DEFAULT_FLEXTALK_URL)
  --ssn-bridge-url <URL>  Override the Social Stream Ninja bridge download (default: $DEFAULT_SSN_BRIDGE_URL)
  --ssn-bridge-ref <ref>  Use a specific git ref for the bridge URL (builds the default URL with that ref)
  --helper-ref <ref>    Use a specific git ref for helper scripts (daemon/gui/flexctl)
  --daemon-url <URL>    Override the NetTTS daemon helper download (default: $DEFAULT_DAEMON_URL)
  --gui-url <URL>       Override the NetTTS GUI helper download (default: $DEFAULT_GUI_URL)
  --flexctl-url <URL>   Override the FlexTalk control panel helper download (default: $DEFAULT_FLEXCTL_URL)
  -h, --help            Show this help text
USAGE
}

error() {
        echo "[ERROR] $*" >&2
        exit 1
}

warn() {
        echo "[WARN] $*" >&2
}

require_cmd() {
        local cmd=$1
        command -v "$cmd" >/dev/null 2>&1 || error "Required command '$cmd' is not available"
}

bridge_url_for_ref() {
        local ref=$1
        printf 'https://raw.githubusercontent.com/h4rm0n1c/NetTTS/%s/scripts/winetricks/ssn_nettts_bridge.py' "$ref"
}

helper_url_for_ref() {
        local ref=$1
        local path=$2
        printf 'https://raw.githubusercontent.com/h4rm0n1c/NetTTS/%s/%s' "$ref" "$path"
}

ROOT_DIR="$DEFAULT_ROOT_DIR"
ROOT_DIR_SET=0
ORIG_WINEPREFIX=${WINEPREFIX:-}
WINEPREFIX_OVERRIDE=0
if [[ -n "$ORIG_WINEPREFIX" ]]; then
        WINEPREFIX="$ORIG_WINEPREFIX"
        WINEPREFIX_OVERRIDE=1
else
        WINEPREFIX="$ROOT_DIR/wineprefix"
fi
WINESERVER_BIN=${WINESERVER:-wineserver}
WINE_BIN=${WINE:-wine}
NETTTS_URL="$DEFAULT_NETTTS_URL"
SAPI_URL="$DEFAULT_SAPI_URL"
FLEXTALK_URL="$DEFAULT_FLEXTALK_URL"
SSN_BRIDGE_REF="$DEFAULT_SSN_BRIDGE_REF"
SSN_BRIDGE_URL="$DEFAULT_SSN_BRIDGE_URL"
HELPER_REF="$DEFAULT_HELPER_REF"
DAEMON_URL="$DEFAULT_DAEMON_URL"
GUI_URL="$DEFAULT_GUI_URL"
FLEXCTL_URL="$DEFAULT_FLEXCTL_URL"

while [[ $# -gt 0 ]]; do
        case $1 in
        --root-dir)
                shift
                [[ $# -gt 0 ]] || error "--root-dir requires a value"
                ROOT_DIR=$1
                ROOT_DIR_SET=1
                if (( ! WINEPREFIX_OVERRIDE )); then
                        WINEPREFIX="$ROOT_DIR/wineprefix"
                fi
                ;;
        --wineprefix)
                shift
                [[ $# -gt 0 ]] || error "--wineprefix requires a value"
                WINEPREFIX=$1
                WINEPREFIX_OVERRIDE=1
                ;;
        --wineserver)
                shift
                [[ $# -gt 0 ]] || error "--wineserver requires a value"
                WINESERVER_BIN=$1
                ;;
        --wine-bin)
                shift
                [[ $# -gt 0 ]] || error "--wine-bin requires a value"
                WINE_BIN=$1
                ;;
        --nettts-url)
                shift
                [[ $# -gt 0 ]] || error "--nettts-url requires a value"
                NETTTS_URL=$1
                ;;
        --sapi-url)
                shift
                [[ $# -gt 0 ]] || error "--sapi-url requires a value"
                SAPI_URL=$1
                ;;
        --flextalk-url)
                shift
                [[ $# -gt 0 ]] || error "--flextalk-url requires a value"
                FLEXTALK_URL=$1
                ;;
        --ssn-bridge-url)
                shift
                [[ $# -gt 0 ]] || error "--ssn-bridge-url requires a value"
                SSN_BRIDGE_URL=$1
                ;;
        --ssn-bridge-ref)
                shift
                [[ $# -gt 0 ]] || error "--ssn-bridge-ref requires a value"
                SSN_BRIDGE_REF=$1
                SSN_BRIDGE_URL=""
                ;;
        --helper-ref)
                shift
                [[ $# -gt 0 ]] || error "--helper-ref requires a value"
                HELPER_REF=$1
                DAEMON_URL=""
                GUI_URL=""
                FLEXCTL_URL=""
                ;;
        --daemon-url)
                shift
                [[ $# -gt 0 ]] || error "--daemon-url requires a value"
                DAEMON_URL=$1
                ;;
        --gui-url)
                shift
                [[ $# -gt 0 ]] || error "--gui-url requires a value"
                GUI_URL=$1
                ;;
        --flexctl-url)
                shift
                [[ $# -gt 0 ]] || error "--flexctl-url requires a value"
                FLEXCTL_URL=$1
                ;;
        -h|--help)
                usage
                exit 0
                ;;
        *)
                error "Unknown argument: $1"
                ;;
        esac
        shift
done

if [[ -z "$SSN_BRIDGE_URL" ]]; then
        SSN_BRIDGE_URL=$(bridge_url_for_ref "$SSN_BRIDGE_REF")
fi

if [[ -z "$DAEMON_URL" ]]; then
        DAEMON_URL=$(helper_url_for_ref "$HELPER_REF" "scripts/winetricks/nettts-daemon.sh")
fi

if [[ -z "$GUI_URL" ]]; then
        GUI_URL=$(helper_url_for_ref "$HELPER_REF" "scripts/winetricks/nettts-gui.sh")
fi

if [[ -z "$FLEXCTL_URL" ]]; then
        FLEXCTL_URL=$(helper_url_for_ref "$HELPER_REF" "scripts/winetricks/flextalk-controlpanel.sh")
fi

mkdir -p "$ROOT_DIR"
ROOT_DIR=$(cd "$ROOT_DIR" && pwd -P)
if (( ! WINEPREFIX_OVERRIDE )); then
        WINEPREFIX="$ROOT_DIR/wineprefix"
fi

require_cmd "$WINE_BIN"
require_cmd winetricks
require_cmd unzip
require_cmd curl
require_cmd winepath
require_cmd "$WINESERVER_BIN"
export WINEPREFIX
export WINESERVER=$WINESERVER_BIN

TMPDIR=$(mktemp -d)
cleanup() {
        rm -rf "$TMPDIR"
}
handle_interrupt() {
        warn "Interrupt received; attempting to stop running Wine processes"
        "$WINESERVER_BIN" -k >/dev/null 2>&1 || true
        exit 130
}

trap cleanup EXIT
trap handle_interrupt INT TERM


download_payload() {
        local source=$1
        local destination=$2
        local label=$3

        if [[ -f "$source" ]]; then
                printf '[INFO] Using local %s at %s\n' "$label" "$source"
                cp -f "$source" "$destination"
                return
        fi

        if [[ "$source" == file://* ]]; then
                local file_path=${source#file://}
                [[ -f "$file_path" ]] || error "$label not found at $file_path"
                printf '[INFO] Using local %s at %s\n' "$label" "$file_path"
                cp -f "$file_path" "$destination"
                return
        fi

        printf '[INFO] Downloading %s from %s...\n' "$label" "$source"
        curl -fL "$source" -o "$destination"
}

PREFIX_CREATED=0
if [[ ! -d "$WINEPREFIX" || ! -f "$WINEPREFIX/system.reg" ]]; then
        export WINEARCH=win32
        PREFIX_CREATED=1
fi

mkdir -p "$WINEPREFIX"

printf '\n[INFO] Preparing Wine prefix at %s\n' "$WINEPREFIX"

printf '[INFO] Installing winxp, vcrun6, mfc42, and riched20 via winetricks...\n'
winetricks -q winxp vcrun6 mfc42 riched20

SAPI_INSTALLER="$TMPDIR/sapi4_runtime.exe"
download_payload "$SAPI_URL" "$SAPI_INSTALLER" "SAPI runtime"
printf '[INFO] Running SAPI 4.0 runtime installer...\n'
"$WINE_BIN" "$SAPI_INSTALLER"
"$WINESERVER_BIN" -w

FLEXTALK_ARCHIVE="$TMPDIR/flextalk.zip"
download_payload "$FLEXTALK_URL" "$FLEXTALK_ARCHIVE" "FlexTalk archive"
printf '[INFO] Extracting FlexTalk voice installer...\n'
unzip -q "$FLEXTALK_ARCHIVE" -d "$TMPDIR"
FLEXTALK_SETUP=$(find "$TMPDIR" -maxdepth 3 -type f -iname 'setup.exe' | head -n 1)
[[ -n "$FLEXTALK_SETUP" ]] || error "Could not locate FlexTalk setup.exe inside archive"

FLEXTALK_SETUP_DIR=$(cd "$(dirname "$FLEXTALK_SETUP")" && pwd -P)
FLEXTALK_SETUP_EXE=$(basename "$FLEXTALK_SETUP")

FLEXTALK_INSTALL_WIN='C:\\Program Files\\Watson21'
printf '[INFO] FlexTalk target directory (default): %s\n' "$FLEXTALK_INSTALL_WIN"

printf '[INFO] Launching FlexTalk installer with GUI...\n'
printf '[INFO] Complete the FlexTalk setup manually; this script will continue afterwards.\n'
pushd "$FLEXTALK_SETUP_DIR" >/dev/null
if ! "$WINE_BIN" "$FLEXTALK_SETUP_EXE"; then
        status=$?
        warn "FlexTalk installer exited with status $status"
        warn "Resolve the installer issue and rerun the script."
        error "FlexTalk installation failed"
fi
popd >/dev/null

if ! "$WINESERVER_BIN" -w; then
        warn "wineserver -w reported an issue after the FlexTalk installer; continuing"
fi

FLEXTALK_INSTALL_UNIX=$(winepath -u "$FLEXTALK_INSTALL_WIN")
if [[ -d "$FLEXTALK_INSTALL_UNIX" ]]; then
        printf '[INFO] FlexTalk installed at %s\n' "$FLEXTALK_INSTALL_UNIX"
else
        warn "FlexTalk not detected at $FLEXTALK_INSTALL_WIN; confirm the GUI install completed successfully."
fi

NETTTS_DIR="$WINEPREFIX/drive_c/nettts"
mkdir -p "$NETTTS_DIR"

NETTTS_TMP=$(mktemp -p "$TMPDIR" nettts.XXXXXX)
NETTTS_FILENAME=$(basename "${NETTTS_URL%%[?#]*}")
NETTTS_EXT="${NETTTS_FILENAME##*.}"
if [[ -z "$NETTTS_FILENAME" || "$NETTTS_EXT" == "$NETTTS_FILENAME" ]]; then
        error "Unable to determine file type from NetTTS download URL: $NETTTS_URL"
fi
NETTTS_DOWNLOAD="$NETTTS_TMP.$NETTTS_EXT"
mv "$NETTTS_TMP" "$NETTTS_DOWNLOAD"
download_payload "$NETTTS_URL" "$NETTTS_DOWNLOAD" "NetTTS payload"

NETTTS_TARGET_BASENAME="nettts_gui.exe"
NETTTS_SOURCE=""
case "${NETTTS_EXT,,}" in
zip)
        NETTTS_EXTRACT_DIR="$TMPDIR/nettts_zip"
        mkdir -p "$NETTTS_EXTRACT_DIR"
        unzip -q "$NETTTS_DOWNLOAD" -d "$NETTTS_EXTRACT_DIR"
        NETTTS_SOURCE=$(find "$NETTTS_EXTRACT_DIR" -maxdepth 3 -type f -iname 'nettts_gui.exe' | head -n 1)
        if [[ -z "$NETTTS_SOURCE" ]]; then
                NETTTS_SOURCE=$(find "$NETTTS_EXTRACT_DIR" -maxdepth 3 -type f -iname '*.exe' | head -n 1)
        fi
        [[ -n "$NETTTS_SOURCE" ]] || error "No executable found inside NetTTS archive"
        NETTTS_TARGET_BASENAME=$(basename "$NETTTS_SOURCE")
        ;;
exe)
        NETTTS_SOURCE="$NETTTS_DOWNLOAD"
        NETTTS_TARGET_BASENAME="$NETTTS_FILENAME"
        ;;
*)
        error "Unsupported NetTTS download type: .$NETTTS_EXT"
        ;;
esac

NETTTS_TARGET="$NETTTS_DIR/$NETTTS_TARGET_BASENAME"

cp -f "$NETTTS_SOURCE" "$NETTTS_TARGET"
chmod +x "$NETTTS_TARGET" || true

TARGET_WIN_PATH=$(winepath -w "$NETTTS_TARGET")
WORKING_WIN_PATH=$(winepath -w "$NETTTS_DIR")
SHORTCUT_DIR="$WINEPREFIX/drive_c/users/Public/Start Menu/Programs"
mkdir -p "$SHORTCUT_DIR"
SHORTCUT_PATH="$SHORTCUT_DIR/NetTTS.lnk"
SHORTCUT_WIN_PATH=$(winepath -w "$SHORTCUT_PATH")

SHORTCUT_VBS=$(mktemp -p "$TMPDIR" shortcut.XXXXXX.vbs)
SHORTCUT_VBS_WIN_PATH=$(winepath -w "$SHORTCUT_VBS")
cat >"$SHORTCUT_VBS" <<'VBS'
Set shell = WScript.CreateObject("WScript.Shell")
Set shortcut = shell.CreateShortcut(WScript.Arguments(0))
shortcut.TargetPath = WScript.Arguments(1)
shortcut.WorkingDirectory = WScript.Arguments(2)
shortcut.IconLocation = WScript.Arguments(3)
shortcut.WindowStyle = 1
shortcut.Save
VBS

printf '[INFO] Creating Start Menu shortcut...\n'
SHORTCUT_CREATED=0
if "$WINE_BIN" wscript.exe "$SHORTCUT_VBS_WIN_PATH" "$SHORTCUT_WIN_PATH" "$TARGET_WIN_PATH" "$WORKING_WIN_PATH" "$TARGET_WIN_PATH"; then
        "$WINESERVER_BIN" -w
        printf '[INFO] Shortcut created at %s\n' "$SHORTCUT_PATH"
        SHORTCUT_CREATED=1
else
        warn 'Unable to create Start Menu shortcut automatically; wscript.exe may be missing.'
        warn "NetTTS is still installed at $NETTTS_DIR; create a shortcut manually if needed."
fi

if (( SHORTCUT_CREATED )); then
        SHORTCUT_STATUS="$SHORTCUT_PATH"
else
        SHORTCUT_STATUS='not created; see warnings above'
fi

BASE_DIR="$ROOT_DIR"
BIN_DIR="$BASE_DIR/bin"
CONFIG_DIR="$BASE_DIR/etc"
RUN_DIR="$BASE_DIR/run"
CONFIG_FILE="$CONFIG_DIR/nettts-daemon.conf"

mkdir -p "$BIN_DIR" "$CONFIG_DIR" "$RUN_DIR"

SSN_BRIDGE_TARGET="$BIN_DIR/ssn_nettts_bridge.py"
printf '[INFO] Preparing Social Stream Ninja bridge from %s (ref: %s)\n' "$SSN_BRIDGE_URL" "$SSN_BRIDGE_REF"
download_payload "$SSN_BRIDGE_URL" "$SSN_BRIDGE_TARGET" "Social Stream Ninja bridge"
chmod +x "$SSN_BRIDGE_TARGET" || true

NETTTS_DAEMON_TARGET="$BIN_DIR/nettts-daemon.sh"
printf '[INFO] Preparing NetTTS daemon helper from %s (ref: %s)\n' "$DAEMON_URL" "$HELPER_REF"
download_payload "$DAEMON_URL" "$NETTTS_DAEMON_TARGET" "NetTTS daemon helper"
chmod +x "$NETTTS_DAEMON_TARGET" || true

NETTTS_GUI_TARGET="$BIN_DIR/nettts-gui.sh"
printf '[INFO] Preparing NetTTS GUI helper from %s (ref: %s)\n' "$GUI_URL" "$HELPER_REF"
download_payload "$GUI_URL" "$NETTTS_GUI_TARGET" "NetTTS GUI helper"
chmod +x "$NETTTS_GUI_TARGET" || true

FLEXCTL_TARGET="$BIN_DIR/flextalk-controlpanel.sh"
printf '[INFO] Preparing FlexTalk control panel helper from %s (ref: %s)\n' "$FLEXCTL_URL" "$HELPER_REF"
download_payload "$FLEXCTL_URL" "$FLEXCTL_TARGET" "FlexTalk control panel helper"
chmod +x "$FLEXCTL_TARGET" || true

if [[ ! -f "$CONFIG_FILE" ]]; then
        cat >"$CONFIG_FILE" <<EOF
# NetTTS daemon configuration
# HOST/PORT define the TCP endpoint for --startserver mode.
# DEVICE selects the FlexTalk output device index (-1 = default mapper).
# VOX_MODE accepts 'vox', 'voxclean', or 'off'.
HOST=127.0.0.1
PORT=5555
DEVICE=-1
VOX_MODE=vox
EOF
fi
cat <<EOF

[DONE] NetTTS prefix ready at: $WINEPREFIX
- Base directory: $BASE_DIR
- Wine prefix: $WINEPREFIX
- NetTTS executable: $NETTTS_TARGET
- Start Menu shortcut: $SHORTCUT_STATUS
- Helper scripts: $BIN_DIR/nettts-daemon.sh, $BIN_DIR/nettts-gui.sh, $BIN_DIR/flextalk-controlpanel.sh, $SSN_BRIDGE_TARGET
- Config file: $CONFIG_FILE
- Log file: $WINEPREFIX/drive_c/nettts/nettts.log (Windows path: C:\\nettts\\nettts.log)

Launch example:
  WINEPREFIX="$WINEPREFIX" "$WINE_BIN" "$NETTTS_TARGET"

If you prefer wrun:
  WINEPREFIX="$WINEPREFIX" wrun "$TARGET_WIN_PATH"
EOF

if (( PREFIX_CREATED )); then
        echo "Note: A fresh 32-bit prefix was created; WINEARCH=win32."
fi
