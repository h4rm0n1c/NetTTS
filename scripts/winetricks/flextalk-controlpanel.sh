#!/usr/bin/env bash
set -euo pipefail

unset LD_PRELOAD LD_LIBRARY_PATH GTK3_MODULES GTK_MODULES GTK_PATH QT_PLUGIN_PATH QT_QPA_PLATFORMTHEME || true

# Example: force PulseAudio/Wine to target a non-default sink (uncomment and edit)
# export FLEXSINK="alsa_output.usb-UC_MIC_ATR2USB-00.analog-stereo"
# export PULSE_SINK="$FLEXSINK"

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

CTL_SYSPATH='C:\\windows\\system32\\flextalk.cpl'

if [[ ${WINE_CMD##*/} == wine ]]; then
        exec env "WINEPREFIX=$WINEPREFIX" "$WINE_CMD" control.exe "$CTL_SYSPATH" "$@"
else
        exec env "WINEPREFIX=$WINEPREFIX" "$WINE_CMD" control.exe "$CTL_SYSPATH" "$@"
fi
