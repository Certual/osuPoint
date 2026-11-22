#!/bin/bash
# osuPoint Linux System Installer
set -e

if [ "$EUID" -ne 0 ]; then
    echo "[ERROR] Please run with root privileges: sudo ./install.sh"
    exit 1
fi

PREFIX="${PREFIX:-/usr/local}"
BINDIR="$PREFIX/bin"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Installing osuPoint binaries to $BINDIR..."
install -d "$BINDIR"

if [ -f "$SCRIPT_DIR/osupoint" ]; then
    install -m 755 "$SCRIPT_DIR/osupoint" "$BINDIR/osupoint"
    ln -sf osupoint "$BINDIR/osupointd"
    ln -sf osupoint "$BINDIR/osupoint-gui"
elif [ -f "$SCRIPT_DIR/osupoint-gui" ] && [ -f "$SCRIPT_DIR/osupointd" ]; then
    install -m 755 "$SCRIPT_DIR/osupoint-gui" "$BINDIR/osupoint-gui"
    install -m 755 "$SCRIPT_DIR/osupointd" "$BINDIR/osupointd"
    ln -sf osupoint-gui "$BINDIR/osupoint"
else
    echo "[ERROR] No osuPoint binary found in $SCRIPT_DIR"
    exit 1
fi

echo "==> Installing udev rules for direct HID tablet access and /dev/uinput..."
install -d /etc/udev/rules.d
if [ -f "$SCRIPT_DIR/99-osupoint.rules" ]; then
    install -m 644 "$SCRIPT_DIR/99-osupoint.rules" /etc/udev/rules.d/99-osupoint.rules
elif [ -f "$SCRIPT_DIR/packaging/99-osupoint.rules" ]; then
    install -m 644 "$SCRIPT_DIR/packaging/99-osupoint.rules" /etc/udev/rules.d/99-osupoint.rules
fi

# Ensure uinput module loads at boot
if [ -d /etc/modules-load.d ]; then
    echo "uinput" > /etc/modules-load.d/uinput.conf 2>/dev/null || true
fi
modprobe uinput 2>/dev/null || true

udevadm control --reload-rules 2>/dev/null || true
udevadm trigger 2>/dev/null || true

echo "==> Installing desktop launcher and application icons..."
install -d /usr/share/applications
if [ -f "$SCRIPT_DIR/osupoint.desktop" ]; then
    install -m 644 "$SCRIPT_DIR/osupoint.desktop" /usr/share/applications/osupoint.desktop
elif [ -f "$SCRIPT_DIR/packaging/osupoint.desktop" ]; then
    install -m 644 "$SCRIPT_DIR/packaging/osupoint.desktop" /usr/share/applications/osupoint.desktop
fi

# Helper to find icon files in current or packaging dir
find_icon() {
    local name="$1"
    if [ -f "$SCRIPT_DIR/$name" ]; then
        echo "$SCRIPT_DIR/$name"
    elif [ -f "$SCRIPT_DIR/packaging/$name" ]; then
        echo "$SCRIPT_DIR/packaging/$name"
    fi
}

ICON16=$(find_icon "osupoint_1_16x16x32.png")
ICON22=$(find_icon "osupoint_22x22.png")
ICON24=$(find_icon "osupoint_24x24.png")
ICON32=$(find_icon "osupoint_2_32x32x32.png")
ICON48=$(find_icon "osupoint_3_48x48x32.png")
ICON256=$(find_icon "osupoint_4_256x256x32.png")
ICON_ROOT=$(find_icon "osupoint.png")

if [ -n "$ICON16" ]; then install -d /usr/share/icons/hicolor/16x16/apps && install -m 644 "$ICON16" /usr/share/icons/hicolor/16x16/apps/osupoint.png; fi
if [ -n "$ICON22" ]; then install -d /usr/share/icons/hicolor/22x22/apps && install -m 644 "$ICON22" /usr/share/icons/hicolor/22x22/apps/osupoint.png; fi
if [ -n "$ICON24" ]; then install -d /usr/share/icons/hicolor/24x24/apps && install -m 644 "$ICON24" /usr/share/icons/hicolor/24x24/apps/osupoint.png; fi
if [ -n "$ICON32" ]; then install -d /usr/share/icons/hicolor/32x32/apps && install -m 644 "$ICON32" /usr/share/icons/hicolor/32x32/apps/osupoint.png; fi
if [ -n "$ICON48" ]; then install -d /usr/share/icons/hicolor/48x48/apps && install -m 644 "$ICON48" /usr/share/icons/hicolor/48x48/apps/osupoint.png; fi
if [ -n "$ICON256" ]; then install -d /usr/share/icons/hicolor/256x256/apps && install -m 644 "$ICON256" /usr/share/icons/hicolor/256x256/apps/osupoint.png; fi
if [ -n "$ICON_ROOT" ]; then install -d /usr/share/pixmaps && install -m 644 "$ICON_ROOT" /usr/share/pixmaps/osupoint.png; fi

command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true

echo "==> Installing systemd user service..."
install -d /usr/lib/systemd/user
if [ -f "$SCRIPT_DIR/osupoint.service" ]; then
    install -m 644 "$SCRIPT_DIR/osupoint.service" /usr/lib/systemd/user/osupoint.service
elif [ -f "$SCRIPT_DIR/packaging/osupoint.service" ]; then
    install -m 644 "$SCRIPT_DIR/packaging/osupoint.service" /usr/lib/systemd/user/osupoint.service
fi

echo ""
echo "=========================================================="
echo " osuPoint Linux Ultra-Low Latency Driver & GUI Installed!"
echo "=========================================================="
echo "Executable: $BINDIR/osupoint"
echo ""
echo "Make sure your user is a member of the 'input' group:"
if [ -n "$SUDO_USER" ]; then
    usermod -a -G input "$SUDO_USER" 2>/dev/null || true
    echo "  Added user '$SUDO_USER' to group 'input'."
fi
echo "If you just joined 'input', please log out and log back in."
echo "Launch from terminal: osupoint"
echo "=========================================================="
