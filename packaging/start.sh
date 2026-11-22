#!/bin/bash
# osuPoint Linux Portable Launcher
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$SCRIPT_DIR/osupoint"

if [ ! -f "$BIN" ]; then
    if [ -f "$SCRIPT_DIR/osupoint-gui" ]; then
        BIN="$SCRIPT_DIR/osupoint-gui"
    elif [ -f "$SCRIPT_DIR/osupointd" ]; then
        BIN="$SCRIPT_DIR/osupointd"
    else
        echo "[ERROR] osuPoint binary not found in $SCRIPT_DIR"
        exit 1
    fi
fi

chmod +x "$BIN" 2>/dev/null || true

# Check uinput module
if [ ! -e /dev/uinput ]; then
    echo "[INFO] /dev/uinput does not exist, attempting to load uinput kernel module..."
    if command -v sudo >/dev/null 2>&1; then
        sudo modprobe uinput || true
    elif command -v pkexec >/dev/null 2>&1; then
        pkexec modprobe uinput || true
    fi
fi

# Check permissions for /dev/uinput
if [ -e /dev/uinput ] && [ ! -w /dev/uinput ]; then
    echo "=================================================================="
    echo "[WARNING] Current user does not have write access to /dev/uinput!"
    echo "=================================================================="
    echo "To grant permanent non-root access, run once:"
    echo "  sudo ./install.sh"
    echo "Or manually add your user to the 'input' group:"
    echo "  sudo usermod -a -G input \$USER"
    echo "  (then log out and log back in)"
    echo "=================================================================="
    echo "Attempting to run with elevated permissions..."
    if command -v pkexec >/dev/null 2>&1 && [ -n "$DISPLAY" ]; then
        exec pkexec env DISPLAY="$DISPLAY" XAUTHORITY="$XAUTHORITY" "$BIN" "$@"
    elif command -v sudo >/dev/null 2>&1; then
        exec sudo env DISPLAY="$DISPLAY" XAUTHORITY="$XAUTHORITY" "$BIN" "$@"
    else
        echo "[ERROR] Cannot elevate permissions. Please run as root or install udev rules."
        exit 1
    fi
fi

# Run portable binary
exec "$BIN" "$@"
