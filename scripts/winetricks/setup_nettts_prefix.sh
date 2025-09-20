#!/usr/bin/env bash
set -euo pipefail

DEFAULT_NETTTS_URL="https://github.com/h4rm0n1c/NetTTS/releases/download/v0.95c/nettts_gui.zip"

usage() {
        cat <<USAGE
Usage: setup_nettts_prefix.sh [options]

Prepare a Wine prefix with SAPI 4.0, FlexTalk, and NetTTS installed.

Options:
  --wineprefix <path>   Target Wine prefix (default: \$HOME/.wine-nettts or \$WINEPREFIX if set)
  --wineserver <path>   Override the wineserver binary (default: \$WINESERVER or "wineserver")
  --wine-bin <path>     Override the wine binary (default: \$WINE or "wine")
  --nettts-url <URL>    Override the NetTTS download (default: $DEFAULT_NETTTS_URL)
  -h, --help            Show this help text
USAGE
}

error() {
        echo "[ERROR] $*" >&2
        exit 1
}

require_cmd() {
        local cmd=$1
        command -v "$cmd" >/dev/null 2>&1 || error "Required command '$cmd' is not available"
}

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
SAPI_INSTALLER="$REPO_ROOT/third_party/Dependencies/spchapi.exe"
FLEXTALK_ARCHIVE="$REPO_ROOT/third_party/Dependencies/flextalk.zip"

[[ -f "$SAPI_INSTALLER" ]] || error "Missing SAPI installer at $SAPI_INSTALLER"
[[ -f "$FLEXTALK_ARCHIVE" ]] || error "Missing FlexTalk archive at $FLEXTALK_ARCHIVE"

DEFAULT_PREFIX=${WINEPREFIX:-"$HOME/.wine-nettts"}
DEFAULT_WINESERVER=${WINESERVER:-wineserver}
DEFAULT_WINE_BIN=${WINE:-wine}

WINEPREFIX="$DEFAULT_PREFIX"
WINESERVER_BIN="$DEFAULT_WINESERVER"
WINE_BIN="$DEFAULT_WINE_BIN"
NETTTS_URL="$DEFAULT_NETTTS_URL"

while [[ $# -gt 0 ]]; do
        case $1 in
        --wineprefix)
                shift
                [[ $# -gt 0 ]] || error "--wineprefix requires a value"
                WINEPREFIX=$1
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

require_cmd "$WINE_BIN"
require_cmd winetricks
require_cmd unzip
require_cmd curl
require_cmd winepath
require_cmd "$WINESERVER_BIN"

export WINEPREFIX
export WINESERVER=$WINESERVER_BIN

PREFIX_CREATED=0
if [[ ! -d "$WINEPREFIX" || ! -f "$WINEPREFIX/system.reg" ]]; then
        export WINEARCH=win32
        PREFIX_CREATED=1
fi

mkdir -p "$WINEPREFIX"

printf '\n[INFO] Preparing Wine prefix at %s\n' "$WINEPREFIX"

printf '[INFO] Installing winxp, vcrun6, and mfc42 via winetricks...\n'
winetricks -q winxp vcrun6 mfc42

printf '[INFO] Running SAPI 4.0 runtime installer...\n'
"$WINE_BIN" start /unix /wait "$SAPI_INSTALLER"
"$WINESERVER_BIN" -w

TMPDIR=$(mktemp -d)
cleanup() {
        rm -rf "$TMPDIR"
}
trap cleanup EXIT

printf '[INFO] Extracting FlexTalk voice installer...\n'
unzip -q "$FLEXTALK_ARCHIVE" -d "$TMPDIR"
FLEXTALK_SETUP=$(find "$TMPDIR" -maxdepth 3 -type f -iname 'setup.exe' | head -n 1)
[[ -n "$FLEXTALK_SETUP" ]] || error "Could not locate FlexTalk setup.exe inside archive"

printf '[INFO] Running FlexTalk installer...\n'
"$WINE_BIN" start /unix /wait "$FLEXTALK_SETUP"
"$WINESERVER_BIN" -w

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
printf '[INFO] Downloading NetTTS from %s...\n' "$NETTTS_URL"
curl -fL "$NETTTS_URL" -o "$NETTTS_DOWNLOAD"

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
cat >"$SHORTCUT_VBS" <<'VBS'
Set shell = WScript.CreateObject("WScript.Shell")
Set shortcut = shell.CreateShortcut(WScript.Arguments(0))
shortcut.TargetPath = WScript.Arguments(1)
shortcut.WorkingDirectory = WScript.Arguments(2)
shortcut.IconLocation = WScript.Arguments(1)
shortcut.WindowStyle = 1
shortcut.Save
VBS

printf '[INFO] Creating Start Menu shortcut...\n'
"$WINE_BIN" wscript.exe "$SHORTCUT_VBS" "$SHORTCUT_WIN_PATH" "$TARGET_WIN_PATH" "$WORKING_WIN_PATH" "$TARGET_WIN_PATH"
"$WINESERVER_BIN" -w

cat <<EOF

[DONE] NetTTS prefix ready at: $WINEPREFIX
- NetTTS executable: $NETTTS_TARGET
- Start Menu shortcut: $SHORTCUT_PATH

Launch example:
  WINEPREFIX="$WINEPREFIX" "$WINE_BIN" start /unix "$NETTTS_TARGET"

If you prefer wrun:
  WINEPREFIX="$WINEPREFIX" wrun "$TARGET_WIN_PATH"
EOF

if (( PREFIX_CREATED )); then
        echo "Note: A fresh 32-bit prefix was created (WINEARCH=win32)."
fi
