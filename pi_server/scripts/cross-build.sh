#!/usr/bin/env bash
# ============================================================
#  Cross-compile rover_server for ARM64 (aarch64)
#  Run this on your powerful desktop/laptop — NOT the Pi.
#
#  Prerequisites (Ubuntu/Debian host):
#    sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#    sudo apt install cmake ninja-build pkg-config
#
#  This builds a static binary that can be scp'd to the Pi.
#
#  Usage:
#    ./cross-build.sh
#    scp ../build-cross/rover_server root@<PI_IP>:/usr/local/bin/
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$PROJECT_DIR/build-cross"
SYSROOT_DIR="$PROJECT_DIR/sysroot-arm64"

echo "=== Cross-compiling rover_server for aarch64 ==="

# ---- 1. Install cross-compiler if missing --------------------
if ! command -v aarch64-linux-gnu-g++ &>/dev/null; then
    echo "[cross] Installing ARM64 cross-compiler..."
    sudo apt-get update
    sudo apt-get install -y \
        gcc-aarch64-linux-gnu \
        g++-aarch64-linux-gnu \
        cmake ninja-build pkg-config
fi

# ---- 2. Create a minimal sysroot with ARM64 libs ------------
#  We need headers + .so for: libgpiod, libjpeg, libssl
#  The easiest way: pull them from the Pi via rsync
echo ""
echo "[cross] You need ARM64 headers/libs. Two options:"
echo "  (a) rsync from the Pi:  ./cross-build.sh --sync root@<PI_IP>"
echo "  (b) Use Docker multiarch (see below)"
echo ""

SYNC_TARGET=""
if [[ "${1:-}" == "--sync" && -n "${2:-}" ]]; then
    SYNC_TARGET="$2"
    echo "[cross] Syncing sysroot from $SYNC_TARGET ..."
    mkdir -p "$SYSROOT_DIR"
    rsync -az --delete \
        "$SYNC_TARGET":/usr/include/ "$SYSROOT_DIR/usr/include/"
    rsync -az --delete \
        "$SYNC_TARGET":/usr/lib/aarch64-linux-gnu/ "$SYSROOT_DIR/usr/lib/aarch64-linux-gnu/"
    rsync -az \
        "$SYNC_TARGET":/usr/local/include/ "$SYSROOT_DIR/usr/local/include/" 2>/dev/null || true
    rsync -az \
        "$SYNC_TARGET":/usr/local/lib/ "$SYSROOT_DIR/usr/local/lib/" 2>/dev/null || true
    echo "[cross] Sysroot ready at $SYSROOT_DIR"
fi

# ---- 3. CMake cross-compile ---------------------------------
mkdir -p "$BUILD_DIR"

cat > "$BUILD_DIR/toolchain-aarch64.cmake" << 'TOOLCHAIN'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_STRIP        aarch64-linux-gnu-strip)

# Sysroot (set via -DSYSROOT_DIR=... or default)
if(DEFINED SYSROOT_DIR)
    set(CMAKE_SYSROOT "${SYSROOT_DIR}")
    set(CMAKE_FIND_ROOT_PATH "${SYSROOT_DIR}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
TOOLCHAIN

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$BUILD_DIR/toolchain-aarch64.cmake" \
    -DSYSROOT_DIR="$SYSROOT_DIR" \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -GNinja

cmake --build "$BUILD_DIR" --parallel

# Strip for smaller binary
aarch64-linux-gnu-strip "$BUILD_DIR/rover_server"

SIZE=$(du -h "$BUILD_DIR/rover_server" | cut -f1)
echo ""
echo "=== Build complete ==="
echo "  Binary: $BUILD_DIR/rover_server  ($SIZE)"
echo ""
echo "  Deploy to Pi:"
echo "    scp $BUILD_DIR/rover_server root@<PI_IP>:/usr/local/bin/"
echo "    ssh root@<PI_IP> systemctl restart rover"
