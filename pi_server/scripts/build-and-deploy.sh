#!/usr/bin/env bash
# ============================================================
#  Build + Package + Deploy — run on your LOCAL machine
#
#  This script:
#    1. Cross-compiles rover_server for aarch64 (the Pi)
#    2. Compiles Teensy firmware via PlatformIO → .hex
#    3. Bundles everything into rover-update.tar.gz
#    4. Deploys to the Pi via scp + ssh
#
#  Prerequisites (install once on your local machine):
#    # ARM64 cross-compiler
#    sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#    sudo apt install cmake ninja-build
#
#    # PlatformIO CLI (for Teensy firmware)
#    pip install platformio
#
#    # ARM64 sysroot libs (pulled from Pi once):
#    ./build-and-deploy.sh --sync-sysroot
#
#  Usage:
#    ./build-and-deploy.sh                    # build only
#    ./build-and-deploy.sh --deploy PI_IP     # build + deploy
#    ./build-and-deploy.sh --sync-sysroot     # pull libs from Pi first
#
#  Examples:
#    ./build-and-deploy.sh --deploy 192.168.86.119
#    ./build-and-deploy.sh --deploy root@192.168.86.119
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PI_SERVER_DIR="$ROOT_DIR/pi_server"
TEENSY_DIR="$ROOT_DIR/teensy_firmware"
BUILD_DIR="$PI_SERVER_DIR/build-cross"
SYSROOT_DIR="$PI_SERVER_DIR/sysroot-arm64"
PACKAGE_DIR="$ROOT_DIR/update-package"

PI_USER="root"
PI_IP=""
DO_DEPLOY=false
DO_SYNC=false

# ---- Parse args ---------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --deploy)
            DO_DEPLOY=true
            PI_TARGET="${2:-}"
            if [[ "$PI_TARGET" == *@* ]]; then
                PI_USER="${PI_TARGET%%@*}"
                PI_IP="${PI_TARGET#*@}"
            else
                PI_IP="$PI_TARGET"
            fi
            shift 2
            ;;
        --sync-sysroot)
            DO_SYNC=true
            PI_TARGET="${2:-}"
            if [[ -z "$PI_TARGET" ]]; then
                echo "Usage: $0 --sync-sysroot [user@]PI_IP"
                exit 1
            fi
            if [[ "$PI_TARGET" == *@* ]]; then
                PI_USER="${PI_TARGET%%@*}"
                PI_IP="${PI_TARGET#*@}"
            else
                PI_IP="$PI_TARGET"
            fi
            shift 2
            ;;
        *)
            echo "Unknown arg: $1"
            exit 1
            ;;
    esac
done

# ---- Sync sysroot from Pi (one-time setup) ------------------
if $DO_SYNC; then
    echo "=== Syncing ARM64 sysroot from $PI_USER@$PI_IP ==="
    mkdir -p "$SYSROOT_DIR/usr"
    rsync -az --info=progress2 \
        "$PI_USER@$PI_IP":/usr/include/ "$SYSROOT_DIR/usr/include/"
    rsync -az --info=progress2 \
        "$PI_USER@$PI_IP":/usr/lib/aarch64-linux-gnu/ "$SYSROOT_DIR/usr/lib/aarch64-linux-gnu/" 2>/dev/null || \
    rsync -az --info=progress2 \
        "$PI_USER@$PI_IP":/usr/lib/arm-linux-gnueabihf/ "$SYSROOT_DIR/usr/lib/arm-linux-gnueabihf/" 2>/dev/null || true
    # Also grab /usr/local (sdbus-c++ if built from source)
    rsync -az --info=progress2 \
        "$PI_USER@$PI_IP":/usr/local/include/ "$SYSROOT_DIR/usr/local/include/" 2>/dev/null || true
    rsync -az --info=progress2 \
        "$PI_USER@$PI_IP":/usr/local/lib/ "$SYSROOT_DIR/usr/local/lib/" 2>/dev/null || true
    echo "[sync] Sysroot ready at $SYSROOT_DIR"
    echo "[sync] You can now run: $0 --deploy $PI_USER@$PI_IP"
    if ! $DO_DEPLOY; then exit 0; fi
fi

echo "=== Building Rover Update Package ==="
echo "  Pi server:  $PI_SERVER_DIR"
echo "  Teensy FW:  $TEENSY_DIR"
echo ""

# ---- 1. Cross-compile rover_server for ARM64 ----------------
echo "--- Step 1: Cross-compile rover_server ---"

if ! command -v aarch64-linux-gnu-g++ &>/dev/null; then
    echo "ERROR: ARM64 cross-compiler not found."
    echo "  Install: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    exit 1
fi

mkdir -p "$BUILD_DIR"

# Generate toolchain file
cat > "$BUILD_DIR/toolchain-aarch64.cmake" << 'TOOLCHAIN'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_STRIP        aarch64-linux-gnu-strip)

if(DEFINED SYSROOT_DIR AND EXISTS "${SYSROOT_DIR}")
    set(CMAKE_SYSROOT "${SYSROOT_DIR}")
    set(CMAKE_FIND_ROOT_PATH "${SYSROOT_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${SYSROOT_DIR}/usr/local")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
TOOLCHAIN

cmake -S "$PI_SERVER_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$BUILD_DIR/toolchain-aarch64.cmake" \
    -DSYSROOT_DIR="$SYSROOT_DIR" \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -GNinja

cmake --build "$BUILD_DIR" --parallel "$(nproc)"
aarch64-linux-gnu-strip "$BUILD_DIR/rover_server"

echo "[build] rover_server: $(du -h "$BUILD_DIR/rover_server" | cut -f1)"

# ---- 2. Build Teensy firmware (.hex) -------------------------
echo ""
echo "--- Step 2: Build Teensy firmware ---"

TEENSY_HEX=""
if command -v pio &>/dev/null; then
    (cd "$TEENSY_DIR" && pio run -e teensy41)
    TEENSY_HEX="$TEENSY_DIR/.pio/build/teensy41/firmware.hex"
    echo "[build] Teensy hex: $(du -h "$TEENSY_HEX" | cut -f1)"
else
    echo "[WARN] PlatformIO not installed — skipping Teensy build."
    echo "       Install: pip install platformio"
    echo "       The update package will NOT contain Teensy firmware."
fi

# ---- 3. Package everything ----------------------------------
echo ""
echo "--- Step 3: Create update package ---"

rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

# rover_server binary
cp "$BUILD_DIR/rover_server" "$PACKAGE_DIR/"

# Teensy firmware (if built)
if [ -n "$TEENSY_HEX" ] && [ -f "$TEENSY_HEX" ]; then
    cp "$TEENSY_HEX" "$PACKAGE_DIR/teensy_firmware.hex"
fi

# Config / service files
cp "$PI_SERVER_DIR/scripts/rover.service"      "$PACKAGE_DIR/"
cp "$PI_SERVER_DIR/scripts/rover.conf.example"  "$PACKAGE_DIR/"
cp "$PI_SERVER_DIR/scripts/deploy-prebuilt.sh"  "$PACKAGE_DIR/" 2>/dev/null || true

# Apply script (runs on Pi)
cat > "$PACKAGE_DIR/apply-update.sh" << 'APPLY'
#!/usr/bin/env bash
# ============================================================
#  apply-update.sh — run on the Pi to install the update
#  Usage:  sudo ./apply-update.sh [--flash-teensy]
# ============================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FLASH_TEENSY=false
[[ "${1:-}" == "--flash-teensy" ]] && FLASH_TEENSY=true

echo "=== Applying Rover Update ==="

# Stop the service
systemctl stop rover.service 2>/dev/null || true

# Install rover_server binary
if [ -f "$SCRIPT_DIR/rover_server" ]; then
    install -m 755 "$SCRIPT_DIR/rover_server" /usr/local/bin/rover_server
    echo "[update] Installed rover_server"
fi

# Update config (only if none exists)
mkdir -p /etc/rover /opt/rover/sounds /tmp/rover_ota
if [ ! -f /etc/rover/rover.conf ] && [ -f "$SCRIPT_DIR/rover.conf.example" ]; then
    cp "$SCRIPT_DIR/rover.conf.example" /etc/rover/rover.conf
    echo "[update] Created /etc/rover/rover.conf — edit for your hardware!"
fi

# Update systemd service
if [ -f "$SCRIPT_DIR/rover.service" ]; then
    cp "$SCRIPT_DIR/rover.service" /etc/systemd/system/
    systemctl daemon-reload
    systemctl enable rover.service
fi

# Flash Teensy firmware (if requested and binary exists)
if $FLASH_TEENSY && [ -f "$SCRIPT_DIR/teensy_firmware.hex" ]; then
    echo "[update] Flashing Teensy..."
    if command -v teensy_loader_cli &>/dev/null; then
        # Read MCU from config or default to TEENSY41
        MMCU="TEENSY41"
        if [ -f /etc/rover/rover.conf ]; then
            MMCU=$(grep '^teensy_mmcu' /etc/rover/rover.conf | cut -d= -f2 | tr -d ' ' || echo "TEENSY41")
        fi
        teensy_loader_cli --mcu="$MMCU" -w -v "$SCRIPT_DIR/teensy_firmware.hex"
        echo "[update] Teensy flashed successfully"
    else
        echo "[WARN] teensy_loader_cli not found — skipping Teensy flash"
        echo "       Install: apt install libhidapi-dev && build from source"
    fi
elif [ -f "$SCRIPT_DIR/teensy_firmware.hex" ]; then
    echo "[update] Teensy firmware included but --flash-teensy not passed"
    echo "         Run:  sudo ./apply-update.sh --flash-teensy"
fi

# Restart
systemctl restart rover.service
echo ""
echo "=== Update Applied ==="
systemctl --no-pager status rover.service || true
APPLY
chmod +x "$PACKAGE_DIR/apply-update.sh"

# Create the tarball
TARBALL="$ROOT_DIR/rover-update.tar.gz"
tar -czf "$TARBALL" -C "$ROOT_DIR" update-package/
echo ""
echo "[package] Created: $TARBALL ($(du -h "$TARBALL" | cut -f1))"
echo "  Contents:"
ls -lh "$PACKAGE_DIR/"

# ---- 4. Deploy to Pi (if --deploy) --------------------------
if $DO_DEPLOY && [ -n "$PI_IP" ]; then
    echo ""
    echo "--- Step 4: Deploying to $PI_USER@$PI_IP ---"
    scp "$TARBALL" "$PI_USER@$PI_IP":/tmp/rover-update.tar.gz
    ssh "$PI_USER@$PI_IP" 'cd /tmp && tar xzf rover-update.tar.gz && cd update-package && chmod +x apply-update.sh && sudo ./apply-update.sh --flash-teensy'
    echo ""
    echo "=== Deploy Complete ==="
    echo "  Check: ssh $PI_USER@$PI_IP journalctl -u rover -f"
else
    echo ""
    echo "=== Package ready — deploy manually: ==="
    echo "  scp $TARBALL root@PI_IP:/tmp/"
    echo "  ssh root@PI_IP 'cd /tmp && tar xzf rover-update.tar.gz && cd update-package && sudo ./apply-update.sh --flash-teensy'"
fi
