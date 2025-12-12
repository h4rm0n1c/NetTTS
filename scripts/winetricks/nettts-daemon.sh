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

LOG_FILE=${NETTTS_LOG_FILE:-/home/harri/nettts/etc/nettts.log}
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

hide_desktop_window() {
        local desktop_name=$1
        local hide=${NETTTS_HIDE_DESKTOP:-1}
        [[ "$hide" == "0" || "$hide" == "false" ]] && return 0

        if ! command -v wmctrl >/dev/null 2>&1; then
                warn "NETTTS_HIDE_DESKTOP is enabled but 'wmctrl' is not installed; desktop window may remain visible"
                return 0
        fi

        for _ in {1..10}; do
                local win_id
                win_id=$(wmctrl -l | awk -v name="$desktop_name" 'index($0, name) {print $1; exit}')
                if [[ -n "$win_id" ]]; then
                        wmctrl -ir "$win_id" -b add,hidden,skip_taskbar,skip_pager,below || true
                        return 0
                fi
                sleep 0.25
        done
        warn "Could not locate virtual desktop window '$desktop_name' to hide"
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
        if kill -0 -"$pid" >/dev/null 2>&1; then
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

        mkdir -p "$CONFIG_DIR"

        local wine_debug status
        wine_debug=$(wine_debug_value)

        # IMPORTANT: no pipes here – Wine/NetTTS behaves differently when stdout is a pipe.
        env WINEPREFIX="$WINEPREFIX" \
            WINEDEBUG="$wine_debug" \
            "$WINE_CMD" "$NETTTS_EXE" --console --list-devices

        status=$?

        if [[ ! -s "$DEVICES_FILE" ]]; then
                warn "NetTTS produced no output for --console --list-devices; see $DEVICES_FILE"
                return "$status"
        fi

        if [[ "$status" -eq 0 ]]; then
                log "Captured device list at $DEVICES_FILE"
                return 0
        else
                warn "Device enumeration failed (exit $status); see $DEVICES_FILE"
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

        local base_cmd=("$NETTTS_EXE" --startserver --headless --host "$HOST" --port "$PORT")
        if [[ -n "$DEVICE" && "$DEVICE" != "-1" ]]; then
                base_cmd+=(--devnum "$DEVICE")
        fi
        case "$VOX_MODE" in
        vox) base_cmd+=(--vox) ;;
        voxclean) base_cmd+=(--voxclean) ;;
        off|none|disabled|"") ;;
        *) warn "Unknown VOX_MODE '$VOX_MODE'; leaving VOX disabled" ;;
        esac

        local launch_mode=${NETTTS_LAUNCH_MODE:-direct}
        local cmd desktop_name desktop_size
        case "$launch_mode" in
        desktop)
                desktop_name=${NETTTS_DESKTOP_NAME:-NetTTS-Desktop}
                desktop_size=${NETTTS_DESKTOP_SIZE:-640x480}
                # /b keeps cmd from opening a console window, /wait preserves the PID so stop works, /min keeps
                # the desktop minimized by default.
                cmd=("$WINE_CMD" cmd /c start /wait /min /b "" explorer "/desktop=${desktop_name},${desktop_size}" "${base_cmd[@]}")
                ;;
        direct|"")
                cmd=("$WINE_CMD" "${base_cmd[@]}")
                ;;
        *)
                warn "Unknown NETTTS_LAUNCH_MODE '$launch_mode'; defaulting to direct"
                cmd=("$WINE_CMD" "${base_cmd[@]}")
                ;;
        esac

        local wine_debug
        wine_debug=$(wine_debug_value)
        nohup setsid env "WINEPREFIX=$WINEPREFIX" "WINESERVER=$WINESERVER_CMD" "WINEDEBUG=$wine_debug" "${cmd[@]}" >>"$LOG_FILE" 2>&1 &
        local pid=$!
        printf '%s\n' "$pid" >"$PID_FILE"
        log "NetTTS daemon started; PID $pid"

        if [[ "$launch_mode" == "desktop" ]]; then
                hide_desktop_window "$desktop_name" &
        fi
}

stop_daemon() {
        local pid
        pid=$(read_pid) || {
                warn "No PID file present; daemon not running?"
                return 0
        }
        if kill -0 -"$pid" >/dev/null 2>&1; then
                kill -- -"$pid" >/dev/null 2>&1 || true
                for _ in {1..10}; do
                        if ! kill -0 -"$pid" >/dev/null 2>&1; then
                                break
                        fi
                        sleep 1
                done
                if kill -0 -"$pid" >/dev/null 2>&1; then
                        warn "Process $pid still alive; sending SIGKILL"
                        kill -9 -- -"$pid" >/dev/null 2>&1 || true
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
                update_device_list
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
