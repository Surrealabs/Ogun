#!/usr/bin/env bash
# ============================================================
#  apply-update.sh — Run ON the Pi to install an Ogun update
#
#  This script is bundled inside every release tarball.
#  It installs rover_server, updates configs, and optionally
#  flashes the Teensy.
#
#  Usage:
#    sudo ./apply-update.sh                  # update Pi only
#    sudo ./apply-update.sh --flash-teensy   # also flash Teensy
# ============================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FLASH_TEENSY=false
ENABLE_AUTO_UPDATE=false
DISABLE_AUTO_UPDATE=false
for arg in "$@"; do
    case "$arg" in
        --flash-teensy) FLASH_TEENSY=true ;;
        --enable-auto-update) ENABLE_AUTO_UPDATE=true ;;
        --disable-auto-update) DISABLE_AUTO_UPDATE=true ;;
        --help|-h)
            echo "Usage: sudo $0 [--flash-teensy] [--enable-auto-update|--disable-auto-update]"
            exit 0
            ;;
    esac
done

echo "=== Applying Ogun Update ==="
if [ -f "$SCRIPT_DIR/BUILD" ]; then
    echo "  Build: $(cat "$SCRIPT_DIR/BUILD")"
fi

# Stop the service
systemctl stop rover.service 2>/dev/null || true

# Install rover_server binary
if [ -f "$SCRIPT_DIR/rover_server" ]; then
    install -m 755 "$SCRIPT_DIR/rover_server" /usr/local/bin/rover_server
    echo "[update] Installed rover_server → /usr/local/bin/"
fi

# Install ogun CLI
if [ -f "$SCRIPT_DIR/ogun" ]; then
    install -m 755 "$SCRIPT_DIR/ogun" /usr/local/bin/ogun
    echo "[update] Installed ogun CLI → /usr/local/bin/ogun"
fi

# Create directories
mkdir -p /etc/rover /opt/rover/sounds /tmp/rover_ota

# Update config (preserve existing — never overwrite user config)
if [ ! -f /etc/rover/rover.conf ] && [ -f "$SCRIPT_DIR/rover.conf.example" ]; then
    cp "$SCRIPT_DIR/rover.conf.example" /etc/rover/rover.conf
    echo "[update] Created /etc/rover/rover.conf — edit for your hardware!"
fi

# Always update the example (for reference)
if [ -f "$SCRIPT_DIR/rover.conf.example" ]; then
    cp "$SCRIPT_DIR/rover.conf.example" /etc/rover/rover.conf.example
fi

# WebUI assets
if [ -d "$SCRIPT_DIR/webui" ]; then
    mkdir -p /opt/rover/webui
    cp -r "$SCRIPT_DIR/webui/"* /opt/rover/webui/
    echo "[update] Updated WebUI assets"
fi

# Update systemd service
if [ -f "$SCRIPT_DIR/rover.service" ]; then
    cp "$SCRIPT_DIR/rover.service" /etc/systemd/system/
fi

# Optional auto-update script + timer files
if [ -f "$SCRIPT_DIR/rover-auto-update.sh" ]; then
    install -m 755 "$SCRIPT_DIR/rover-auto-update.sh" /usr/local/bin/rover-auto-update.sh
fi
if [ -f "$SCRIPT_DIR/rover-auto-update.service" ]; then
    cp "$SCRIPT_DIR/rover-auto-update.service" /etc/systemd/system/
fi
if [ -f "$SCRIPT_DIR/rover-auto-update.timer" ]; then
    cp "$SCRIPT_DIR/rover-auto-update.timer" /etc/systemd/system/
fi
if [ -f "$SCRIPT_DIR/rover-auto-update.env.example" ] && [ ! -f /etc/default/rover-auto-update ]; then
    mkdir -p /etc/default
    cp "$SCRIPT_DIR/rover-auto-update.env.example" /etc/default/rover-auto-update
fi

systemctl daemon-reload
systemctl enable rover.service

if $ENABLE_AUTO_UPDATE; then
    systemctl enable --now rover-auto-update.timer
    echo "[update] Enabled rover-auto-update.timer"
fi
if $DISABLE_AUTO_UPDATE; then
    systemctl disable --now rover-auto-update.timer 2>/dev/null || true
    echo "[update] Disabled rover-auto-update.timer"
fi

# Install Teensy firmware hex for later flashing
if [ -f "$SCRIPT_DIR/teensy_firmware.hex" ]; then
    mkdir -p /opt/rover/firmware
    cp "$SCRIPT_DIR/teensy_firmware.hex" /opt/rover/firmware/teensy_firmware.hex
    echo "[update] Installed teensy_firmware.hex → /opt/rover/firmware/"
fi

# Flash Teensy firmware
if [ -f "$SCRIPT_DIR/teensy_firmware.hex" ]; then
    if $FLASH_TEENSY; then
        echo "[update] Flashing Teensy..."
        if command -v teensy_loader_cli &>/dev/null; then
            MMCU="TEENSY40"
            if [ -f /etc/rover/rover.conf ]; then
                CFG_MMCU=$(grep '^teensy_mmcu' /etc/rover/rover.conf | cut -d= -f2 | tr -d ' ')
                [ -n "$CFG_MMCU" ] && MMCU="$CFG_MMCU"
            fi
            # -s uses the loader's built-in soft reboot path (Teensy 3.x/4.x)
            # so we do not depend on a separate teensy_reboot binary.
            teensy_loader_cli --mcu="$MMCU" -w -s -v "$SCRIPT_DIR/teensy_firmware.hex"
            echo "[update] Teensy flashed successfully"
        else
            echo "[warn] teensy_loader_cli not found — skipping Teensy flash"
            echo "       Install via: sudo ./ogun deps"
        fi
    else
        echo "[update] Teensy firmware included — pass --flash-teensy to apply"
    fi
fi

# Record installed version
if [ -f "$SCRIPT_DIR/BUILD" ]; then
    cp "$SCRIPT_DIR/BUILD" /opt/rover/INSTALLED_VERSION
fi

# Restart
systemctl restart rover.service
echo ""
echo "=== Update Applied ==="
systemctl --no-pager status rover.service || true
