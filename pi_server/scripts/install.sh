#!/usr/bin/env bash
# ============================================================
#  Rover Pi Server — Dependency Installer & Build Script
#  Run as root on the BTT Pi 1.2 (Debian/Ubuntu-based OS)
#
#  SAFE FOR LOW-RAM DEVICES (1 GB):
#    - Creates swap if < 1.5 GB total available
#    - Limits parallel compile jobs to 1
#    - Cleans up between build steps
# ============================================================
set -euo pipefail

echo "=== Rover Server Install ==="

# ---- 0. Ensure enough memory (swap) -------------------------
TOTAL_MEM_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
TOTAL_SWAP_KB=$(grep SwapTotal /proc/meminfo | awk '{print $2}')
TOTAL_AVAIL=$(( TOTAL_MEM_KB + TOTAL_SWAP_KB ))

if [ "$TOTAL_AVAIL" -lt 1500000 ]; then
    echo "[install] Low memory detected ($(( TOTAL_AVAIL / 1024 )) MB total)."
    echo "[install] Creating 1 GB swap file..."
    if [ ! -f /swapfile ]; then
        dd if=/dev/zero of=/swapfile bs=1M count=1024 status=progress
        chmod 600 /swapfile
        mkswap /swapfile
    fi
    swapon /swapfile 2>/dev/null || true
    # Make persistent
    if ! grep -q '/swapfile' /etc/fstab; then
        echo '/swapfile none swap sw 0 0' >> /etc/fstab
    fi
    echo "[install] Swap active: $(free -h | grep Swap)"
fi

# Limit parallel jobs — 1 per 700 MB of total RAM+swap to avoid OOM
AVAIL_MB=$(( (TOTAL_MEM_KB + TOTAL_SWAP_KB) / 1024 ))
MAX_JOBS=$(( AVAIL_MB / 700 ))
[ "$MAX_JOBS" -lt 1 ] && MAX_JOBS=1
[ "$MAX_JOBS" -gt 4 ] && MAX_JOBS=4
echo "[install] Using $MAX_JOBS parallel compile job(s) (${AVAIL_MB} MB avail)"

# ---- 1. Update package lists --------------------------------
apt-get update

# ---- 2. Build tools ----------------------------------------
apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    ninja-build

# ---- 3. Runtime dependencies --------------------------------
apt-get install -y \
    libgpiod-dev \
    libudev-dev \
    libjpeg-dev \
    libssl-dev \
    libdbus-1-dev \
    bluez \
    bluez-tools \
    python3-dbus

# ---- 4. sdbus-c++ >= 2.0 ------------------------------------
# Available in sid/bookworm or build from source
if ! pkg-config --exists sdbus-c++-2 2>/dev/null; then
    echo "[install] Building sdbus-c++ from source (single-threaded to avoid OOM)..."
    SDBUS_TMP=$(mktemp -d)
    git clone --depth=1 --branch v2.1.0 \
        https://github.com/Kistler-Group/sdbus-cpp.git "$SDBUS_TMP/sdbus-cpp"
    cmake -S "$SDBUS_TMP/sdbus-cpp" -B "$SDBUS_TMP/sdbus-cpp/build" \
          -DCMAKE_BUILD_TYPE=MinSizeRel \
          -DBUILD_SHARED_LIBS=ON \
          -DBUILD_TESTS=OFF \
          -DBUILD_DOC=OFF
    cmake --build "$SDBUS_TMP/sdbus-cpp/build" -j"$MAX_JOBS"
    cmake --install "$SDBUS_TMP/sdbus-cpp/build"
    ldconfig
    rm -rf "$SDBUS_TMP"
    # Free pagecache after heavy compile
    sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
    echo "[install] sdbus-c++ installed."
fi

# ---- 5. Teensy loader CLI (for OTA flashing) ----------------
if ! command -v teensy_loader_cli &>/dev/null; then
    echo "[install] Installing teensy_loader_cli..."
    apt-get install -y libhidapi-dev
    TLC_TMP=$(mktemp -d)
    git clone --depth=1 \
        https://github.com/PaulStoffregen/teensy_loader_cli.git "$TLC_TMP/tlc"
    make -C "$TLC_TMP/tlc" -j1
    cp "$TLC_TMP/tlc/teensy_loader_cli" /usr/local/bin/
    rm -rf "$TLC_TMP"
fi

# ---- 6. Add Teensy udev rule --------------------------------
cat > /etc/udev/rules.d/49-teensy.rules << 'EOF'
ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="0478", ENV{ID_MM_DEVICE_IGNORE}="1"
ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="0483", ENV{ID_MM_DEVICE_IGNORE}="1"
KERNEL=="ttyACM*", ATTRS{idVendor}=="16c0", MODE="0666"
EOF
udevadm control --reload-rules
udevadm trigger

# ---- 7. Enable Bluetooth at boot ---------------------------
systemctl enable bluetooth
systemctl start bluetooth
btmgmt le on     2>/dev/null || true
btmgmt connectable on 2>/dev/null || true
btmgmt advertising on 2>/dev/null || true

# ---- 8. Build rover_server ---------------------------------
echo "[install] Building rover_server with $MAX_JOBS job(s)..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"
mkdir -p "$BUILD_DIR"
cmake -S "$SCRIPT_DIR/.." -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=MinSizeRel \
      -GNinja
cmake --build "$BUILD_DIR" -j"$MAX_JOBS"
cmake --install "$BUILD_DIR"

# ---- 9. Create config and sounds dirs ----------------------
mkdir -p /etc/rover /opt/rover/sounds /tmp/rover_ota
if [ ! -f /etc/rover/rover.conf ]; then
    cp "$SCRIPT_DIR/rover.conf.example" /etc/rover/rover.conf
    echo "[install] Created /etc/rover/rover.conf — edit it to match your hardware"
fi

# ---- 10. Install and start systemd service -----------------
systemctl daemon-reload
systemctl enable rover.service
systemctl restart rover.service

echo ""
echo "=== Installation Complete ==="
echo "  Status:     systemctl status rover"
echo "  Logs:       journalctl -u rover -f"
echo "  Config:     /etc/rover/rover.conf"
echo "  Web UI:     http://<ip>:8080"
echo "  Cameras:    http://<ip>:8081  http://<ip>:8082"
echo "  WebSocket:  ws://<ip>:9000"
