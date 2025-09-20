#!/usr/bin/env bash
set -euo pipefail

unset LD_PRELOAD LD_LIBRARY_PATH GTK3_MODULES GTK_MODULES GTK_PATH QT_PLUGIN_PATH QT_QPA_PLATFORMTHEME || true

DEFAULT_NETTTS_URL="https://github.com/h4rm0n1c/NetTTS/releases/download/v0.95c/nettts_gui.zip"
DEFAULT_SAPI_URL="https://github.com/h4rm0n1c/NetTTS/raw/refs/heads/main/third_party/Dependencies/spchapi.exe"
DEFAULT_FLEXTALK_URL="https://github.com/h4rm0n1c/NetTTS/raw/refs/heads/main/third_party/Dependencies/flextalk.zip"
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
trap cleanup EXIT

SCRIPT_DIR=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd -P)
FLEXTALK_RESPONSE_SOURCE="$REPO_ROOT/third_party/Dependencies/flextalk_setup.iss"
[[ -f "$FLEXTALK_RESPONSE_SOURCE" ]] || error "Bundled FlexTalk response file not found at $FLEXTALK_RESPONSE_SOURCE"

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

printf '[INFO] Installing winxp, vcrun6, and mfc42 via winetricks...\n'
winetricks -q winxp vcrun6 mfc42

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
FLEXTALK_SETUP_ISS="$FLEXTALK_SETUP_DIR/SETUP.ISS"
cp -f "$FLEXTALK_RESPONSE_SOURCE" "$FLEXTALK_SETUP_ISS"
printf '[INFO] Using FlexTalk response file from %s\n' "$FLEXTALK_RESPONSE_SOURCE"

FLEXTALK_INSTALL_WIN=$(awk -F= '/^szDir[[:space:]]*=/{gsub(/\r$/, "", $2); print $2}' "$FLEXTALK_RESPONSE_SOURCE" | tail -n 1)
if [[ -z "$FLEXTALK_INSTALL_WIN" ]]; then
        FLEXTALK_INSTALL_WIN='C:\\Program Files\\Watson21'
fi
if [[ ${FLEXTALK_INSTALL_WIN: -1} != \\ ]]; then
        FLEXTALK_INSTALL_WIN+="\\"
fi
printf '[INFO] FlexTalk target directory: %s\n' "$FLEXTALK_INSTALL_WIN"

FLEXTALK_LOG="$TMPDIR/flextalk-install.log"
FLEXTALK_WIN_LOG=$(winepath -w "$FLEXTALK_LOG")
FLEXTALK_WIN_ISS=$(winepath -w "$FLEXTALK_SETUP_ISS")

printf '[INFO] Running FlexTalk installer silently...\n'
(
        cd "$FLEXTALK_SETUP_DIR"
        "$WINE_BIN" "$FLEXTALK_SETUP_EXE" /s /SMS "/f1$FLEXTALK_WIN_ISS" "/f2$FLEXTALK_WIN_LOG"
)
"$WINESERVER_BIN" -w

FLEXTALK_INSTALL_UNIX=$(winepath -u "$FLEXTALK_INSTALL_WIN")
if [[ ! -d "$FLEXTALK_INSTALL_UNIX" ]]; then
        warn "FlexTalk silent installer did not create $FLEXTALK_INSTALL_WIN"
        if [[ -f "$FLEXTALK_LOG" ]]; then
                warn "Review the InstallShield log at $FLEXTALK_LOG for details"
        fi
        error "FlexTalk installation failed"
fi
printf '[INFO] FlexTalk installed at %s\n' "$FLEXTALK_INSTALL_UNIX"

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
DEVICES_FILE="$CONFIG_DIR/nettts-devices.txt"

mkdir -p "$BIN_DIR" "$CONFIG_DIR" "$RUN_DIR"

if [[ ! -f "$CONFIG_FILE" ]]; then
        cat >"$CONFIG_FILE" <<EOF
# NetTTS daemon configuration
# HOST/PORT define the TCP endpoint for --startserver mode.
# DEVICE selects the FlexTalk output device index (-1 = default mapper).
# VOX_MODE accepts 'vox', 'voxclean', or 'off'.
# See $DEVICES_FILE for the most recently captured device listing.
HOST=127.0.0.1
PORT=5555
DEVICE=-1
VOX_MODE=vox
EOF
fi

DEVICE_LIST_OUTPUT=""
if DEVICE_LIST_OUTPUT=$(env WINEPREFIX="$WINEPREFIX" "$WINE_BIN" "$NETTTS_TARGET" --headless --list-devices 2>&1); then
        printf '%s\n' "$DEVICE_LIST_OUTPUT" >"$DEVICES_FILE"
else
        warn "Failed to enumerate output devices; captured output in $DEVICES_FILE"
        printf '%s\n' "$DEVICE_LIST_OUTPUT" >"$DEVICES_FILE"
fi

cat >"$BIN_DIR/nettts-daemon.sh" <<'DAEMON'
#!/usr/bin/env bash
set -euo pipefail

unset LD_PRELOAD LD_LIBRARY_PATH GTK3_MODULES GTK_MODULES GTK_PATH QT_PLUGIN_PATH QT_QPA_PLATFORMTHEME || true

SCRIPT_DIR=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
BASE_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd -P)
DEFAULT_WINEPREFIX="$BASE_DIR/wineprefix"

if [[ -n ${WINEPREFIX:-} ]]; then
        TARGET_WINEPREFIX=$WINEPREFIX
else
        TARGET_WINEPREFIX="$DEFAULT_WINEPREFIX"
fi
export WINEPREFIX="$TARGET_WINEPREFIX"

if [[ -n ${NETTTS_WINE_BIN:-} ]]; then
        WINE_CMD=$NETTTS_WINE_BIN
elif [[ -n ${WINE_BIN:-} ]]; then
        WINE_CMD=$WINE_BIN
else
        WINE_CMD=wine
fi

if [[ -n ${NETTTS_WINESERVER:-} ]]; then
        WINESERVER_CMD=$NETTTS_WINESERVER
elif [[ -n ${WINESERVER:-} ]]; then
        WINESERVER_CMD=$WINESERVER
else
        WINESERVER_CMD=wineserver
fi

LOG_FILE=${NETTTS_LOG_FILE:-/var/log/nettts.log}
NC_BIN=${NETTTS_NC_BIN:-nc}
NC_TIMEOUT=${NETTTS_NC_TIMEOUT:-3}
CONFIG_DIR="$BASE_DIR/etc"
CONFIG_FILE="$CONFIG_DIR/nettts-daemon.conf"
DEVICES_FILE="$CONFIG_DIR/nettts-devices.txt"
RUN_DIR="$BASE_DIR/run"
PID_FILE="$RUN_DIR/nettts-daemon.pid"
NETTTS_EXE="$WINEPREFIX/drive_c/nettts/nettts_gui.exe"

usage() {
        cat <<'USAGE'
Usage: nettts-daemon.sh <command> [args]

Commands:
  start             Launch NetTTS headless with the TCP server enabled
  stop              Stop the daemon if it is running
  restart           Stop and then start the daemon
  status            Print whether the daemon is running (exit 0 if running)
  speak <text>      Send text to the TCP server via netcat (or pipe stdin)
  list-devices      Refresh and print the captured audio device list
  show-config       Display the service configuration file
  config-path       Print the configuration file path
  log-path          Print the daemon log file path
  help              Show this help message
USAGE
}

log() {
        printf '[INFO] %s\n' "$*"
}

warn() {
        printf '[WARN] %s\n' "$*" >&2
}

error() {
        printf '[ERROR] %s\n' "$*" >&2
        exit 1
}

require_cmd() {
        local cmd=$1
        command -v "$cmd" >/dev/null 2>&1 || error "Required command '$cmd' is not available"
}

trim() {
        local var=$1
        var="${var#${var%%[![:space:]]*}}"
        var="${var%${var##*[![:space:]]}}"
        printf '%s' "$var"
}

strip_quotes() {
        local var=$1
        local dq=$'\042'
        local sq=$'\047'
        if [[ (${var:0:1} == "$dq" && ${var: -1} == "$dq") || (${var:0:1} == "$sq" && ${var: -1} == "$sq") ]]; then
                var=${var:1:-1}
        fi
        printf '%s' "$var"
}

ensure_config() {
        mkdir -p "$CONFIG_DIR"
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
}

load_config() {
        HOST=127.0.0.1
        PORT=5555
        DEVICE=-1
        VOX_MODE=vox

        if [[ -f "$CONFIG_FILE" ]]; then
                while IFS= read -r line || [[ -n "$line" ]]; do
                        line=$(trim "$line")
                        [[ -z "$line" ]] && continue
                        [[ ${line:0:1} == '#' ]] && continue
                        local key=${line%%=*}
                        local value=${line#*=}
                        key=$(trim "$key")
                        value=$(trim "$value")
                        value=${value%%#*}
                        value=$(trim "$value")
                        value=$(strip_quotes "$value")
                        case "$key" in
                        HOST) HOST=$value ;;
                        PORT) PORT=$value ;;
                        DEVICE) DEVICE=$value ;;
                        VOX_MODE) VOX_MODE=$value ;;
                        esac
                done <"$CONFIG_FILE"
        fi
}

ensure_log_file() {
        if [[ ! -e "$LOG_FILE" ]]; then
                if ! touch "$LOG_FILE" 2>/dev/null; then
                        error "Cannot create log file $LOG_FILE; adjust permissions or set NETTTS_LOG_FILE"
                fi
        fi
        if [[ ! -w "$LOG_FILE" ]]; then
                error "Log file $LOG_FILE is not writable"
        fi
}

check_executable() {
        [[ -f "$NETTTS_EXE" ]] || error "NetTTS executable not found at $NETTTS_EXE"
}

read_pid() {
        [[ -f "$PID_FILE" ]] || return 1
        local pid
        pid=$(<"$PID_FILE")
        [[ -n "$pid" ]] || return 1
        printf '%s' "$pid"
}

is_running() {
        local pid
        pid=$(read_pid) || return 1
        if kill -0 "$pid" >/dev/null 2>&1; then
                return 0
        fi
        rm -f "$PID_FILE"
        return 1
}

wine_debug_value() {
        if [[ -n ${NETTTS_WINEDEBUG:-} ]]; then
                printf '%s' "$NETTTS_WINEDEBUG"
        elif [[ -n ${WINEDEBUG:-} ]]; then
                printf '%s' "$WINEDEBUG"
        else
                printf '%s' "-all"
        fi
}

update_device_list() {
        ensure_config
        check_executable
        require_cmd "$WINE_CMD"
        local output
        if output=$(env "WINEPREFIX=$WINEPREFIX" "$WINE_CMD" "$NETTTS_EXE" --headless --list-devices 2>&1); then
                printf '%s\n' "$output" >"$DEVICES_FILE"
                log "Captured device list at $DEVICES_FILE"
                return 0
        else
                printf '%s\n' "$output" >"$DEVICES_FILE"
                warn "Device enumeration failed; see $DEVICES_FILE"
                return 1
        fi
}

start_daemon() {
        ensure_config
        mkdir -p "$RUN_DIR"
        check_executable
        ensure_log_file
        require_cmd "$WINE_CMD"
        require_cmd "$WINESERVER_CMD"
        if is_running; then
                local current_pid
                current_pid=$(read_pid) || current_pid="?"
                warn "NetTTS daemon already running; PID $current_pid"
                return 0
        fi
        load_config
        update_device_list || true

        local cmd=("$WINE_CMD" "$NETTTS_EXE" --startserver --headless --host "$HOST" --port "$PORT")
        if [[ -n "$DEVICE" && "$DEVICE" != "-1" ]]; then
                cmd+=(--devnum "$DEVICE")
        fi
        case "$VOX_MODE" in
        vox) cmd+=(--vox) ;;
        voxclean) cmd+=(--voxclean) ;;
        off|none|disabled|"") ;;
        *) warn "Unknown VOX_MODE '$VOX_MODE'; leaving VOX disabled" ;;
        esac

        local wine_debug
        wine_debug=$(wine_debug_value)
        nohup env "WINEPREFIX=$WINEPREFIX" "WINESERVER=$WINESERVER_CMD" "WINEDEBUG=$wine_debug" "${cmd[@]}" >>"$LOG_FILE" 2>&1 &
        local pid=$!
        printf '%s\n' "$pid" >"$PID_FILE"
        log "NetTTS daemon started; PID $pid"
}

stop_daemon() {
        local pid
        pid=$(read_pid) || {
                warn "No PID file present; daemon not running?"
                return 0
        }
        if kill -0 "$pid" >/dev/null 2>&1; then
                kill "$pid" >/dev/null 2>&1 || true
                for _ in {1..10}; do
                        if ! kill -0 "$pid" >/dev/null 2>&1; then
                                break
                        fi
                        sleep 1
                done
                if kill -0 "$pid" >/dev/null 2>&1; then
                        warn "Process $pid still alive; sending SIGKILL"
                        kill -9 "$pid" >/dev/null 2>&1 || true
                fi
        else
                warn "Stale PID file for process $pid"
        fi
        rm -f "$PID_FILE"
        env "WINEPREFIX=$WINEPREFIX" "$WINESERVER_CMD" -w >/dev/null 2>&1 || true
        log "NetTTS daemon stopped"
}

status_daemon() {
        if is_running; then
                local current_pid
                current_pid=$(read_pid) || current_pid="?"
                printf 'NetTTS daemon running; PID %s\n' "$current_pid"
                return 0
        fi
        printf 'NetTTS daemon is not running\n'
        return 1
}

speak_command() {
        ensure_config
        load_config
        require_cmd "$NC_BIN"
        local payload
        if [[ $# -gt 0 ]]; then
                payload="$*"
        else
                if [ -t 0 ]; then
                        error "Provide text to speak or pipe input"
                fi
                payload=$(cat)
        fi
        printf '%s\r\n' "$payload" | "$NC_BIN" -w "$NC_TIMEOUT" "$HOST" "$PORT"
}

main() {
        mkdir -p "$RUN_DIR"
        local cmd=${1:-help}
        shift || true
        case "$cmd" in
        start) start_daemon ;;
        stop) stop_daemon ;;
        restart) stop_daemon; start_daemon ;;
        status) status_daemon ;;
        speak) speak_command "$@" ;;
        list-devices)
                local rc=0
                update_device_list || rc=$?
                [[ -f "$DEVICES_FILE" ]] && cat "$DEVICES_FILE"
                return $rc
                ;;
        show-config)
                ensure_config
                cat "$CONFIG_FILE"
                ;;
        config-path)
                ensure_config
                printf '%s\n' "$CONFIG_FILE"
                ;;
        log-path)
                printf '%s\n' "$LOG_FILE"
                ;;
        help|-h|--help)
                usage
                ;;
        *)
                usage >&2
                error "Unknown command: $cmd"
                ;;
        esac
}

main "$@"
DAEMON
chmod +x "$BIN_DIR/nettts-daemon.sh"

cat >"$BIN_DIR/nettts-gui.sh" <<'GUI'
#!/usr/bin/env bash
set -euo pipefail

unset LD_PRELOAD LD_LIBRARY_PATH GTK3_MODULES GTK_MODULES GTK_PATH QT_PLUGIN_PATH QT_QPA_PLATFORMTHEME || true

SCRIPT_DIR=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
BASE_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd -P)
DEFAULT_WINEPREFIX="$BASE_DIR/wineprefix"

if [[ -n ${WINEPREFIX:-} ]]; then
        TARGET_WINEPREFIX=$WINEPREFIX
else
        TARGET_WINEPREFIX="$DEFAULT_WINEPREFIX"
fi
export WINEPREFIX="$TARGET_WINEPREFIX"

if [[ -n ${NETTTS_WINE_BIN:-} ]]; then
        WINE_CMD=$NETTTS_WINE_BIN
elif [[ -n ${WINE_BIN:-} ]]; then
        WINE_CMD=$WINE_BIN
else
        WINE_CMD=wine
fi

NETTTS_EXE="$WINEPREFIX/drive_c/nettts/nettts_gui.exe"

if [[ ! -f "$NETTTS_EXE" ]]; then
        printf '[ERROR] NetTTS executable not found at %s\n' "$NETTTS_EXE" >&2
        exit 1
fi

exec env "WINEPREFIX=$WINEPREFIX" "$WINE_CMD" "$NETTTS_EXE" "$@"
GUI
chmod +x "$BIN_DIR/nettts-gui.sh"

cat >"$BIN_DIR/flextalk-controlpanel.sh" <<'FLEXCTL'
#!/usr/bin/env bash
set -euo pipefail

unset LD_PRELOAD LD_LIBRARY_PATH GTK3_MODULES GTK_MODULES GTK_PATH QT_PLUGIN_PATH QT_QPA_PLATFORMTHEME || true

SCRIPT_DIR=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
BASE_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd -P)
DEFAULT_WINEPREFIX="$BASE_DIR/wineprefix"

if [[ -n ${WINEPREFIX:-} ]]; then
        TARGET_WINEPREFIX=$WINEPREFIX
else
        TARGET_WINEPREFIX="$DEFAULT_WINEPREFIX"
fi
export WINEPREFIX="$TARGET_WINEPREFIX"

if [[ -n ${NETTTS_WINE_BIN:-} ]]; then
        WINE_CMD=$NETTTS_WINE_BIN
elif [[ -n ${WINE_BIN:-} ]]; then
        WINE_CMD=$WINE_BIN
else
        WINE_CMD=wine
fi

CTL_SYSPATH='C:\windows\system32\flextalk.cpl'

if [[ ${WINE_CMD##*/} == wine ]]; then
        exec env "WINEPREFIX=$WINEPREFIX" "$WINE_CMD" control.exe "$CTL_SYSPATH" "$@"
else
        exec env "WINEPREFIX=$WINEPREFIX" "$WINE_CMD" control.exe "$CTL_SYSPATH" "$@"
fi
FLEXCTL
chmod +x "$BIN_DIR/flextalk-controlpanel.sh"

cat <<EOF

[DONE] NetTTS prefix ready at: $WINEPREFIX
- Base directory: $BASE_DIR
- Wine prefix: $WINEPREFIX
- NetTTS executable: $NETTTS_TARGET
- Start Menu shortcut: $SHORTCUT_STATUS
- Helper scripts: $BIN_DIR/nettts-daemon.sh, $BIN_DIR/nettts-gui.sh, $BIN_DIR/flextalk-controlpanel.sh
- Config file: $CONFIG_FILE
- Device list: $DEVICES_FILE
- Log file: /var/log/nettts.log - create it ahead of time if needed

Launch example:
  WINEPREFIX="$WINEPREFIX" "$WINE_BIN" "$NETTTS_TARGET"

If you prefer wrun:
  WINEPREFIX="$WINEPREFIX" wrun "$TARGET_WIN_PATH"
EOF

if (( PREFIX_CREATED )); then
        echo "Note: A fresh 32-bit prefix was created; WINEARCH=win32."
fi
