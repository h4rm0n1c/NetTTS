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

LOG_FILE=${NETTTS_LOG_FILE:-"$WINEPREFIX/drive_c/nettts/nettts.log"}
LOG_FILE_WIN=${NETTTS_LOG_FILE_WIN:-C:\\nettts\\nettts.log}
NC_BIN=${NETTTS_NC_BIN:-nc}
NC_TIMEOUT=${NETTTS_NC_TIMEOUT:-3}
CONFIG_DIR="$BASE_DIR/etc"
CONFIG_FILE="$CONFIG_DIR/nettts-daemon.conf"
RUN_DIR="$BASE_DIR/run"
PID_FILE="$RUN_DIR/nettts-daemon.pid"
NETTTS_EXE="$WINEPREFIX/drive_c/nettts/nettts_gui.exe"
HOME_DIR=${NETTTS_HOME:-${HOME:-$BASE_DIR}}
CACHE_DIR=${NETTTS_CACHE_HOME:-"$BASE_DIR/.cache"}
FC_CACHE_DIR="$CACHE_DIR/fontconfig"

usage() {
        cat <<'USAGE'
Usage: nettts-daemon.sh <command> [args]

Commands:
  start             Launch NetTTS headless with the TCP server enabled
  stop              Stop the daemon if it is running
  restart           Stop and then start the daemon
  status            Print whether the daemon is running (exit 0 if running)
  speak <text>      Send text to the TCP server via netcat (or pipe stdin)
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

ensure_runtime_paths() {
        mkdir -p "$RUN_DIR"
        mkdir -p "$(dirname "$LOG_FILE")"
        mkdir -p "$HOME_DIR" "$CACHE_DIR" "$FC_CACHE_DIR"

        if [[ ! -w "$HOME_DIR" ]]; then
                error "HOME directory $HOME_DIR is not writable; set NETTTS_HOME"
        fi

        if [[ ! -w "$CACHE_DIR" ]]; then
                error "Cache directory $CACHE_DIR is not writable; set NETTTS_CACHE_HOME"
        fi
}

ensure_log_file() {
        if [[ ! -e "$LOG_FILE" ]]; then
                if ! touch "$LOG_FILE" 2>/dev/null; then
                        error "Cannot create log file $LOG_FILE; set NETTTS_LOG_FILE"
                fi
        fi

        if [[ ! -w "$LOG_FILE" ]]; then
                error "Log file $LOG_FILE is not writable; adjust permissions or set NETTTS_LOG_FILE"
        fi
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

check_executable() {
        [[ -f "$NETTTS_EXE" ]] || error "NetTTS executable not found at $NETTTS_EXE"
}

ensure_alsa_fallback() {
        local pipewire_paths=()
        local host_multiarch
        local prefix_arch=${WINEARCH:-unknown}

        if [[ $prefix_arch == unknown && -f "$WINEPREFIX/system.reg" ]]; then
                if grep -q '^#arch=win64' "$WINEPREFIX/system.reg"; then
                        prefix_arch=win64
                else
                        prefix_arch=win32
                fi
        fi

        host_multiarch=$(gcc -print-multiarch 2>/dev/null || true)
        if [[ -n $host_multiarch ]]; then
                pipewire_paths+=("/usr/lib/$host_multiarch/alsa-lib/libasound_module_pcm_pipewire.so")
        fi

        pipewire_paths+=(
                "/usr/lib/alsa-lib/libasound_module_pcm_pipewire.so"
                "/usr/lib/i386-linux-gnu/alsa-lib/libasound_module_pcm_pipewire.so"
                "/usr/lib/i686-linux-gnu/alsa-lib/libasound_module_pcm_pipewire.so"
        )

        local shim_found_any=false
        local shim_found_32=false

        for candidate in "${pipewire_paths[@]}"; do
                if [[ -f "$candidate" ]]; then
                        shim_found_any=true
                        if [[ $candidate == *i386-linux-gnu* || $candidate == *i686-linux-gnu* ]]; then
                                shim_found_32=true
                        fi
                fi
        done

        local shim_needed=true
        if [[ $prefix_arch == win32 ]]; then
                shim_needed=$shim_found_32
        else
                shim_needed=$shim_found_any
        fi

        # Respect any user-provided ALSA overrides and only intervene when the
        # PipeWire ALSA shim is missing (common in minimal service setups).
        if [[ -n ${ALSA_CONFIG_PATH:-} || $shim_needed == true ]]; then
                return
        fi

        mkdir -p "$CONFIG_DIR"
        local fallback_conf="$CONFIG_DIR/alsa-fallback.conf"

        cat >"$fallback_conf" <<'EOF'
@hooks [
        {
                func load
                files [
                        "/usr/share/alsa/alsa.conf"
                ]
                errors false
        }
]

pcm.sysdefault {
        type hw
        card 0
}

ctl.sysdefault {
        type hw
        card 0
}

pcm.!default {
        type plug
        slave.pcm "sysdefault"
}

ctl.!default {
        type sysdefault
}
EOF

        export ALSA_CONFIG_PATH="$fallback_conf"
        export PIPEWIRE_ALSA=0

        warn "PipeWire ALSA shim not found; using fallback ALSA config at $fallback_conf"
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

start_daemon() {
        ensure_config
        ensure_runtime_paths
        ensure_log_file
        check_executable

        ensure_alsa_fallback

        require_cmd "$NC_BIN"

        load_config

        local args=("$NETTTS_EXE" --startserver --host "$HOST" --port "$PORT" --vox "$VOX_MODE" --device "$DEVICE")

        # Keep the launch simple and closer to the long-standing behavior: rely on
        # Wine's `start /min` to avoid a visible console while still detaching the
        # process. Users can override NETTTS_LAUNCH_MODE to "desktop" if they need
        # the virtual desktop path for their environment.
        local launch_mode=${NETTTS_LAUNCH_MODE:-start}
        local wine_start

        if [[ $launch_mode == desktop ]]; then
                local desktop_name=${NETTTS_DESKTOP_NAME:-NetTTS-Headless}
                local desktop_size=${NETTTS_DESKTOP_SIZE:-640x480}
                wine_start=(explorer "/desktop=${desktop_name},${desktop_size}" "${args[@]}")
        else
                wine_start=(start /min /unix "${args[@]}")
        fi

        log "Starting NetTTS daemon..."
        log "Log file: $LOG_FILE (Windows: $LOG_FILE_WIN)"

        if [[ -n ${NETTTS_ENV:-} ]]; then
            IFS=' ' read -r -a extra_env <<<"$NETTTS_ENV"
        else
            extra_env=()
        fi

        local wine_env=(
                "WINEPREFIX=$WINEPREFIX"
                "WINEDEBUG=$(wine_debug_value)"
                "HOME=$HOME_DIR"
                "XDG_CACHE_HOME=$CACHE_DIR"
                "FC_CACHEDIR=$FC_CACHE_DIR"
                "PATH=$PATH"
        )

        if [[ -n ${ALSA_CONFIG_PATH:-} ]]; then
                wine_env+=("ALSA_CONFIG_PATH=$ALSA_CONFIG_PATH")
        fi

        if [[ -n ${PIPEWIRE_ALSA:-} ]]; then
                wine_env+=("PIPEWIRE_ALSA=$PIPEWIRE_ALSA")
        fi

        if [[ ${#extra_env[@]} -gt 0 ]]; then
                wine_env+=(${extra_env[@]})
        fi

        # Preload the wineserver to reduce surface races before launching the GUI.
        "$WINESERVER_CMD" -p >/dev/null 2>&1 || true

        (
                umask 0022
                exec setsid env -i "${wine_env[@]}" "$WINE_CMD" "${wine_start[@]}" \
                        >>"$LOG_FILE" 2>&1 </dev/null
        ) &

        local pid=$!
        printf '%s' "$pid" >"$PID_FILE"
        log "NetTTS started with PID $pid"
}

stop_daemon() {
        if ! is_running; then
                log "NetTTS is not running"
                return 0
        fi

        local pid
        pid=$(read_pid) || return 0

        log "Stopping NetTTS daemon (PID $pid)..."
        if kill "$pid" >/dev/null 2>&1; then
                if ! wait "$pid" 2>/dev/null; then
                        warn "Process $pid did not exit cleanly"
                fi
                rm -f "$PID_FILE"
                log "NetTTS stopped"
        else
                warn "Unable to stop PID $pid; removing stale PID file"
                rm -f "$PID_FILE"
        fi
}

status_daemon() {
        if is_running; then
                log "NetTTS daemon is running (PID $(read_pid))"
        else
                log "NetTTS daemon is not running"
                return 1
        fi
}

speak_command() {
        require_cmd "$NC_BIN"
        ensure_config
        load_config

        local text
        if [[ $# -gt 0 ]]; then
                text="$*"
        else
                text=$(cat)
        fi

        printf '%s\n' "$text" | "$NC_BIN" -w "$NC_TIMEOUT" "$HOST" "$PORT"
}

main() {
        local cmd=${1:-}
        shift || true

        case "$cmd" in
        start) start_daemon ;;
        stop) stop_daemon ;;
        restart)
                stop_daemon
                start_daemon
                ;;
        status) status_daemon ;;
        speak) speak_command "$@" ;;
        show-config)
                ensure_config
                cat "$CONFIG_FILE"
                ;;
        config-path)
                ensure_config
                printf '%s\n' "$CONFIG_FILE"
                ;;
        log-path)
                printf 'Host path: %s\n' "$LOG_FILE"
                printf 'Windows path: %s\n' "$LOG_FILE_WIN"
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
