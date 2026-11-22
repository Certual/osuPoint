#!/bin/bash
# osuPoint Linux System Uninstaller
set -e

if [ "$EUID" -ne 0 ]; then
    echo "[ERROR] Please run with root privileges: sudo ./uninstall.sh"
    exit 1
fi

PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"

echo "==> Removing osuPoint files from $BINDIR..."
rm -f "$BINDIR/osupoint"
rm -f "$BINDIR/osupointd"
rm -f "$BINDIR/osupoint-gui"

echo "==> Removing udev rules, desktop launcher and services..."
rm -f /etc/udev/rules.d/99-osupoint.rules
rm -f /etc/modules-load.d/uinput.conf
rm -f /usr/share/applications/osupoint.desktop
rm -f /usr/lib/systemd/user/osupoint.service

rm -f /usr/share/icons/hicolor/16x16/apps/osupoint.png
rm -f /usr/share/icons/hicolor/22x22/apps/osupoint.png
rm -f /usr/share/icons/hicolor/24x24/apps/osupoint.png
rm -f /usr/share/icons/hicolor/32x32/apps/osupoint.png
rm -f /usr/share/icons/hicolor/48x48/apps/osupoint.png
rm -f /usr/share/icons/hicolor/256x256/apps/osupoint.png
rm -f /usr/share/pixmaps/osupoint.png

udevadm control --reload-rules 2>/dev/null || true
udevadm trigger 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true

echo "==> osuPoint has been uninstalled successfully."
