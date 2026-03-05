#!/usr/bin/env bash
# ============================================================
#  Deploy pre-built rover_server to the Pi
#  Run this on the Pi INSTEAD of install.sh
#
#  This skips all compilation — only installs runtime deps,
#  config, udev rules, and the systemd service.
#
#  Usage:
#    1. Cross-compile on your desktop:  ./cross-build.sh --sync root@PI
#    2. scp build-cross/rover_server to /root/rover/pi_server/build/
#    3. On the Pi:  sudo ./deploy-prebuilt.sh
# ============================================================
set -euo pipefail

echo "=== Rover Deploy (pre-built binary) ==="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/../build/rover_server"

# Check binary exists
if [ ! -f "$BINARY" ]; then
    # Also check build-cross
    BINARY="$SCRIPT_DIR/../build-cross/rover_server"
fi
if [ ! -f "$BINARY" ]; then
    echo "ERROR: rover_server binary not found."
    echo "  Place it at: $SCRIPT_DIR/../build/rover_server"
    echo "  Or cross-compile first, then scp the binary here."
    exit 1
fi

# Verify it's an ARM binary
FILE_TYPE=$(file "$BINARY" 2>/dev/null || echo "unknown")
echo "[deploy] Binary: $BINARY"
echo "[deploy] Type:   $FILE_TYPE"

# ---- 1. Swap (if needed) ------------------------------------
TOTAL_MEM_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
TOTAL_SWAP_KB=$(grep SwapTotal /proc/meminfo | awk '{print $2}')
if [ $(( TOTAL_MEM_KB + TOTAL_SWAP_KB )) -lt 1500000 ]; then
    if [ ! -f /swapfile ]; then
        echo "[deploy] Creating 512 MB swap..."
        dd if=/dev/zero of=/swapfile bs=1M count=512 status=progress
        chmod 600 /swapfile
        mkswap /swapfile
    fi
    swapon /swapfile 2>/dev/null || true
    grep -q '/swapfile' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi

# ---- 2. Runtime-only packages (no build tools needed) -------
apt-get update
apt-get install -y \
    libgpiod2 \
    libjpeg62-turbo \
    libssl3 \
    libdbus-1-3 \
    bluez \
    bluez-tools \
    libhidapi-hidraw0

# Try to install sdbus-c++ runtime lib (may be built from source)
apt-get install -y libsdbus-c++1 2>/dev/null || true

# If sdbus-c++ .so was built from source, it lives in /usr/local/lib
ldconfig

# ---- 3. Install binary --------------------------------------
install -m 755 "$BINARY" /usr/local/bin/rover_server
echo "[deploy] Installed /usr/local/bin/rover_server"

# ---- 4. Teensy udev rules -----------------------------------
cat > /etc/udev/rules.d/49-teensy.rules << 'EOF'
ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="0478", ENV{ID_MM_DEVICE_IGNORE}="1"
ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="0483", ENV{ID_MM_DEVICE_IGNORE}="1"
KERNEL=="ttyACM*", ATTRS{idVendor}=="16c0", MODE="0666"
EOF
udevadm control --reload-rules
udevadm trigger

# ---- 5. Bluetooth setup -------------------------------------
systemctl enable bluetooth
systemctl start bluetooth
btmgmt le on     2>/dev/null || true
btmgmt connectable on 2>/dev/null || true
btmgmt advertising on 2>/dev/null || true

# ---- 6. Config + dirs ---------------------------------------
mkdir -p /etc/rover /opt/rover/sounds /tmp/rover_ota
if [ ! -f /etc/rover/rover.conf ]; then
    cp "$SCRIPT_DIR/rover.conf.example" /etc/rover/rover.conf
    echo "[deploy] Created /etc/rover/rover.conf — EDIT IT for your hardware!"
fi

# ---- 7. systemd service -------------------------------------
cp "$SCRIPT_DIR/rover.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable rover.service
systemctl restart rover.service

echo ""
echo "=== Deploy Complete ==="
echo "  Status:     systemctl status rover"
echo "  Logs:       journalctl -u rover -f"
echo "  Config:     /etc/rover/rover.conf"
echo "  Web UI:     http://$(hostname -I | awk '{print $1}'):8080"
