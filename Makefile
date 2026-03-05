# ============================================================
#  Ogun Rover — Unified Build System
#
#  Targets:
#    make all           Build Pi server + Teensy firmware
#    make pi            Build rover_server (native on Pi)
#    make teensy        Build Teensy .hex via PlatformIO
#    make android       Build Android APK (debug)
#    make android-release  Build Android APK (release)
#    make package       Create rover-update.tar.gz bundle
#    make deploy PI=<ip>  Package + deploy to Pi via SSH
#    make flash-teensy  Flash Teensy via USB (PlatformIO)
#    make install       Install rover_server + systemd unit (run as root on Pi)
#    make cross         Cross-compile Pi server for ARM64
#    make clean         Clean all build artifacts
#    make deps          Install build dependencies (run as root on Pi)
#    make info          Print build environment info
#
#  Variables:
#    PI          Pi IP address (for deploy)
#    PI_USER     SSH user (default: root)
#    JOBS        Parallel jobs (default: auto-detect)
#    BUILD_TYPE  CMake build type (default: MinSizeRel)
#    TEENSY_ENV  PlatformIO env (default: teensy41)
# ============================================================

SHELL       := /bin/bash
.DEFAULT_GOAL := all

# ---------- Paths -------------------------------------------
ROOT_DIR    := $(shell pwd)
PI_DIR      := $(ROOT_DIR)/pi_server
TEENSY_DIR  := $(ROOT_DIR)/teensy_firmware
ANDROID_DIR := $(ROOT_DIR)/android_app
SCRIPTS_DIR := $(PI_DIR)/scripts

# Pi server build dirs
PI_BUILD    := $(PI_DIR)/build
CROSS_BUILD := $(PI_DIR)/build-cross
SYSROOT     := $(PI_DIR)/sysroot-arm64

# ---------- Config ------------------------------------------
PI_USER     ?= root
PI          ?=
BUILD_TYPE  ?= MinSizeRel
TEENSY_ENV  ?= teensy41
JOBS        ?= $(shell nproc 2>/dev/null || echo 2)

# Version
GEN_VERSION := $(ROOT_DIR)/scripts/gen-version.sh

# Output
PACKAGE_DIR := $(ROOT_DIR)/update-package
TARBALL     := $(ROOT_DIR)/rover-update.tar.gz

# Teensy hex output path
TEENSY_HEX  := $(TEENSY_DIR)/.pio/build/$(TEENSY_ENV)/firmware.hex

# Detect if we are running on the Pi (ARM) or desktop (x86)
ARCH        := $(shell uname -m)
IS_PI       := $(filter aarch64 armv7l,$(ARCH))

# ============================================================
#  Phony targets
# ============================================================
.PHONY: all pi teensy android android-release package deploy \
        flash-teensy install cross cross-sync clean clean-pi \
        clean-teensy clean-android clean-package deps info help

# ============================================================
#  all — build Pi server + Teensy firmware
# ============================================================
all: pi teensy

# ============================================================
#  Pi server (native build — run ON the Pi or any Linux box)
# ============================================================
pi: $(PI_BUILD)/rover_server

$(PI_BUILD)/rover_server: $(wildcard $(PI_DIR)/src/*.cpp $(PI_DIR)/src/**/*.cpp $(PI_DIR)/src/**/*.hpp $(PI_DIR)/CMakeLists.txt)
	@echo "=== Building rover_server ($(BUILD_TYPE), $(JOBS) jobs) ==="
	@mkdir -p $(PI_BUILD)
	@$(GEN_VERSION) --cpp $(PI_DIR)/src/Version.hpp
	cmake -S $(PI_DIR) -B $(PI_BUILD) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-GNinja
	cmake --build $(PI_BUILD) -j$(JOBS)
	@echo "[pi] Built: $(PI_BUILD)/rover_server ($(shell du -h $(PI_BUILD)/rover_server 2>/dev/null | cut -f1 || echo 'new'))"

# ============================================================
#  Cross-compile Pi server for ARM64 (run on desktop)
# ============================================================
cross: $(CROSS_BUILD)/rover_server

$(CROSS_BUILD)/rover_server: $(wildcard $(PI_DIR)/src/*.cpp $(PI_DIR)/src/**/*.cpp $(PI_DIR)/src/**/*.hpp $(PI_DIR)/CMakeLists.txt)
	@echo "=== Cross-compiling rover_server for aarch64 ==="
	@command -v aarch64-linux-gnu-g++ >/dev/null || { \
		echo "ERROR: ARM64 cross-compiler not found."; \
		echo "  Install: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"; \
		exit 1; \
	}
	@mkdir -p $(CROSS_BUILD)
	@cat > $(CROSS_BUILD)/toolchain-aarch64.cmake <<- 'TOOLCHAIN' \
	set(CMAKE_SYSTEM_NAME Linux) \
	set(CMAKE_SYSTEM_PROCESSOR aarch64) \
	set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc) \
	set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++) \
	set(CMAKE_STRIP        aarch64-linux-gnu-strip) \
	if(DEFINED SYSROOT_DIR AND EXISTS "$${SYSROOT_DIR}") \
	    set(CMAKE_SYSROOT "$${SYSROOT_DIR}") \
	    set(CMAKE_FIND_ROOT_PATH "$${SYSROOT_DIR}") \
	    list(APPEND CMAKE_PREFIX_PATH "$${SYSROOT_DIR}/usr/local") \
	endif() \
	set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER) \
	set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY) \
	set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY) \
	set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY) \
	TOOLCHAIN
	cmake -S $(PI_DIR) -B $(CROSS_BUILD) \
		-DCMAKE_TOOLCHAIN_FILE=$(CROSS_BUILD)/toolchain-aarch64.cmake \
		-DSYSROOT_DIR=$(SYSROOT) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-GNinja
	cmake --build $(CROSS_BUILD) -j$(JOBS)
	aarch64-linux-gnu-strip $(CROSS_BUILD)/rover_server
	@echo "[cross] Built: $(CROSS_BUILD)/rover_server ($(shell du -h $(CROSS_BUILD)/rover_server 2>/dev/null | cut -f1))"

# Pull ARM64 sysroot from Pi for cross-compilation
cross-sync:
	@test -n "$(PI)" || { echo "Usage: make cross-sync PI=<ip>"; exit 1; }
	@echo "=== Syncing ARM64 sysroot from $(PI_USER)@$(PI) ==="
	@mkdir -p $(SYSROOT)/usr
	rsync -az --info=progress2 $(PI_USER)@$(PI):/usr/include/ $(SYSROOT)/usr/include/
	rsync -az --info=progress2 $(PI_USER)@$(PI):/usr/lib/aarch64-linux-gnu/ $(SYSROOT)/usr/lib/aarch64-linux-gnu/ 2>/dev/null || true
	rsync -az --info=progress2 $(PI_USER)@$(PI):/usr/local/include/ $(SYSROOT)/usr/local/include/ 2>/dev/null || true
	rsync -az --info=progress2 $(PI_USER)@$(PI):/usr/local/lib/ $(SYSROOT)/usr/local/lib/ 2>/dev/null || true
	@echo "[sync] Sysroot ready at $(SYSROOT)"

# ============================================================
#  Teensy firmware (PlatformIO)
# ============================================================
teensy: $(TEENSY_HEX)

$(TEENSY_HEX): $(wildcard $(TEENSY_DIR)/src/*.cpp $(TEENSY_DIR)/src/*.hpp $(TEENSY_DIR)/platformio.ini)
	@echo "=== Building Teensy firmware ($(TEENSY_ENV)) ==="
	@command -v pio >/dev/null || { \
		echo "ERROR: PlatformIO not found."; \
		echo "  Install: pip install platformio"; \
		exit 1; \
	}
	@$(GEN_VERSION) --cpp $(TEENSY_DIR)/src/Version.hpp
	cd $(TEENSY_DIR) && pio run -e $(TEENSY_ENV)
	@echo "[teensy] Built: $(TEENSY_HEX) ($(shell du -h $(TEENSY_HEX) 2>/dev/null | cut -f1))"

# Flash Teensy directly via USB
flash-teensy: teensy
	@echo "=== Flashing Teensy via PlatformIO ==="
	cd $(TEENSY_DIR) && pio run -e $(TEENSY_ENV) --target upload

# ============================================================
#  Android app
# ============================================================
android:
	@echo "=== Building Android APK (debug) ==="
	@test -d $(ANDROID_DIR) || { echo "ERROR: android_app/ not found"; exit 1; }
	cd $(ANDROID_DIR) && ./gradlew assembleDebug
	@echo "[android] APK: $(ANDROID_DIR)/app/build/outputs/apk/debug/"

android-release:
	@echo "=== Building Android APK (release) ==="
	cd $(ANDROID_DIR) && ./gradlew assembleRelease
	@echo "[android] APK: $(ANDROID_DIR)/app/build/outputs/apk/release/"

# ============================================================
#  Package — bundle everything into a deployable tarball
# ============================================================
package:
	@echo "=== Creating update package ==="
	@rm -rf $(PACKAGE_DIR)
	@mkdir -p $(PACKAGE_DIR)
	@# Determine which binary to package
	@if [ -f $(CROSS_BUILD)/rover_server ]; then \
		cp $(CROSS_BUILD)/rover_server $(PACKAGE_DIR)/; \
		echo "[pkg] Using cross-compiled binary"; \
	elif [ -f $(PI_BUILD)/rover_server ]; then \
		cp $(PI_BUILD)/rover_server $(PACKAGE_DIR)/; \
		echo "[pkg] Using native binary"; \
	else \
		echo "ERROR: No rover_server binary found. Run 'make pi' or 'make cross' first."; \
		exit 1; \
	fi
	@# Teensy firmware (optional)
	@if [ -f $(TEENSY_HEX) ]; then \
		cp $(TEENSY_HEX) $(PACKAGE_DIR)/teensy_firmware.hex; \
		echo "[pkg] Including Teensy firmware"; \
	else \
		echo "[pkg] No Teensy firmware found — skipping (run 'make teensy' to include)"; \
	fi
	@# Config / service files
	@cp $(SCRIPTS_DIR)/rover.service     $(PACKAGE_DIR)/
	@cp $(SCRIPTS_DIR)/rover.conf.example $(PACKAGE_DIR)/
	@# WebUI assets
	@if [ -d $(PI_DIR)/webui ]; then \
		cp -r $(PI_DIR)/webui $(PACKAGE_DIR)/webui; \
		echo "[pkg] Including WebUI assets"; \
	fi
	@# Android APK (if built)
	@APK=$$(find $(ANDROID_DIR)/app/build/outputs/apk -name '*.apk' 2>/dev/null | head -1); \
	if [ -n "$$APK" ]; then \
		cp "$$APK" $(PACKAGE_DIR)/ogun-controller.apk; \
		echo "[pkg] Including Android APK"; \
	fi
	@# Generate apply script
	@cp $(ROOT_DIR)/scripts/apply-update.sh $(PACKAGE_DIR)/apply-update.sh
	@chmod +x $(PACKAGE_DIR)/apply-update.sh
	@# Tarball
	tar -czf $(TARBALL) -C $(ROOT_DIR) update-package/
	@echo ""
	@echo "[package] Created: $(TARBALL) ($$(du -h $(TARBALL) | cut -f1))"
	@ls -lh $(PACKAGE_DIR)/

# ============================================================
#  Deploy — package + send to Pi
# ============================================================
deploy: package
	@test -n "$(PI)" || { echo "Usage: make deploy PI=<ip>"; exit 1; }
	@echo "=== Deploying to $(PI_USER)@$(PI) ==="
	scp $(TARBALL) $(PI_USER)@$(PI):/tmp/rover-update.tar.gz
	ssh $(PI_USER)@$(PI) '\
		cd /tmp && \
		tar xzf rover-update.tar.gz && \
		cd update-package && \
		chmod +x apply-update.sh && \
		sudo ./apply-update.sh --flash-teensy'
	@echo ""
	@echo "=== Deploy Complete ==="
	@echo "  Logs:  ssh $(PI_USER)@$(PI) journalctl -u rover -f"

# ============================================================
#  Install — run on the Pi to install the locally-built binary
# ============================================================
install:
	@echo "=== Installing rover_server ==="
	@test -f $(PI_BUILD)/rover_server || { echo "ERROR: Build first with 'make pi'"; exit 1; }
	install -m 755 $(PI_BUILD)/rover_server /usr/local/bin/rover_server
	@# Config
	@mkdir -p /etc/rover /opt/rover/sounds /tmp/rover_ota
	@if [ ! -f /etc/rover/rover.conf ]; then \
		cp $(SCRIPTS_DIR)/rover.conf.example /etc/rover/rover.conf; \
		echo "[install] Created /etc/rover/rover.conf"; \
	fi
	@# WebUI
	@if [ -d $(PI_DIR)/webui ]; then \
		mkdir -p /opt/rover/webui; \
		cp -r $(PI_DIR)/webui/* /opt/rover/webui/; \
	fi
	@# Systemd
	cp $(SCRIPTS_DIR)/rover.service /etc/systemd/system/
	systemctl daemon-reload
	systemctl enable rover.service
	systemctl restart rover.service
	@echo "[install] Done — check: systemctl status rover"

# ============================================================
#  Dependencies — install build deps on the Pi
# ============================================================
deps:
	@echo "=== Installing build dependencies ==="
	$(SCRIPTS_DIR)/install.sh

# ============================================================
#  Clean
# ============================================================
clean: clean-pi clean-teensy clean-package

clean-pi:
	@echo "Cleaning Pi server builds..."
	@rm -rf $(PI_BUILD) $(CROSS_BUILD)

clean-teensy:
	@echo "Cleaning Teensy firmware..."
	@test -d $(TEENSY_DIR) && cd $(TEENSY_DIR) && pio run --target clean 2>/dev/null || rm -rf $(TEENSY_DIR)/.pio

clean-android:
	@echo "Cleaning Android build..."
	@test -d $(ANDROID_DIR) && cd $(ANDROID_DIR) && ./gradlew clean 2>/dev/null || true

clean-package:
	@echo "Cleaning package artifacts..."
	@rm -rf $(PACKAGE_DIR) $(TARBALL)

clean-all: clean clean-android

# ============================================================
#  Info / help
# ============================================================
info:
	@echo "Ogun Rover Build System"
	@echo "======================="
	@echo "  Arch:       $(ARCH) $(if $(IS_PI),(on Pi),(desktop — use 'make cross' for ARM64))"
	@echo "  Jobs:       $(JOBS)"
	@echo "  Build type: $(BUILD_TYPE)"
	@echo "  Teensy env: $(TEENSY_ENV)"
	@echo ""
	@echo "  Pi server:  $(PI_DIR)"
	@echo "  Teensy FW:  $(TEENSY_DIR)"
	@echo "  Android:    $(ANDROID_DIR)"
	@echo ""
	@echo "  Tools:"
	@printf "    cmake:    " && (cmake --version 2>/dev/null | head -1 || echo "NOT FOUND")
	@printf "    ninja:    " && (ninja --version 2>/dev/null || echo "NOT FOUND")
	@printf "    pio:      " && (pio --version 2>/dev/null || echo "NOT FOUND")
	@printf "    aarch64:  " && (aarch64-linux-gnu-g++ --version 2>/dev/null | head -1 || echo "NOT FOUND (optional, for cross-compile)")
	@printf "    gradlew:  " && (test -f $(ANDROID_DIR)/gradlew && echo "found" || echo "NOT FOUND (optional)")

help:
	@echo "Ogun Rover — Build Targets"
	@echo ""
	@echo "  Build:"
	@echo "    make all             Build Pi server + Teensy firmware"
	@echo "    make pi              Build rover_server (native)"
	@echo "    make teensy          Build Teensy .hex via PlatformIO"
	@echo "    make cross           Cross-compile rover_server for ARM64"
	@echo "    make android         Build Android APK (debug)"
	@echo "    make android-release Build Android APK (release)"
	@echo ""
	@echo "  Flash / Deploy:"
	@echo "    make flash-teensy    Flash Teensy via USB"
	@echo "    make package         Bundle update tarball"
	@echo "    make deploy PI=<ip>  Package + deploy to Pi"
	@echo "    make install         Install locally (run on Pi as root)"
	@echo ""
	@echo "  Setup:"
	@echo "    make deps            Install Pi build dependencies (root)"
	@echo "    make cross-sync PI=<ip>  Pull ARM64 sysroot from Pi"
	@echo ""
	@echo "  Clean:"
	@echo "    make clean           Clean Pi + Teensy + package"
	@echo "    make clean-all       Clean everything including Android"
	@echo ""
	@echo "  Info:"
	@echo "    make info            Show build environment"
	@echo "    make help            This help"
