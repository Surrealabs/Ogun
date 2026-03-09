# Rover Control System

A full-stack rover platform built on top of a **BTT Pi 1.2** (Klipper board repurposed),
using a **Teensy 4.1** as a motor/sensor hub, **BTS7960** H-bridge motor drivers,
controlled from an **Android** app (or web wrapper) over **WiFi/WebSocket**.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Android App (Kotlin)                        │
│  ┌──────────────┐  ┌─────────────────────┐                     │
│  │WebSocketMgr  │  │  JoystickView +     │                     │
│  │  OkHttp WS   │  │  Camera MJPEG UI    │                     │
│  └──────┬───────┘  └─────────────────────┘                     │
└─────────┼───────────────────────────────────────────────────────┘
             │ WebSocket (port 9000)
             │ MJPEG   (port 8081 / 8082)
┌─────────┼───────────────────────────────────────────────────────┐
│         │   BTT Pi 1.2                                          │
│  ┌──────┴───────────────────┴──────────────────────────────┐   │
│  │               rover_server (C++)                         │   │
│  │  WifiServer  │  WebUiServer  │  CameraStream ×2          │   │
│  │  (WebSocket) │  (HTTP + WS)  │  (V4L2 + MJPEG HTTP)      │   │
│  │  GpioController (libgpiod)                                │   │
│  │  TeensyOta (teensy_loader_cli)                            │   │
│  └─────────────────────────────┬───────────────────────────┘   │
│                                 │ USB Serial (115200 baud)       │
└─────────────────────────────────┼───────────────────────────────┘
                                  │
                    ┌─────────────┴──────────────┐
                    │       Teensy 4.1            │
                    │  main.cpp (Arduino)          │
                    │  MotorController (BTS7960)  │
                    │  SensorHub (encoders, v/i)  │
                    └─────────────────────────────┘
                         |           |
                    BTS7960 L    BTS7960 R
                       │               │
                    Left Motor     Right Motor
```

---

## Current Bring-Up Mode (Mar 2026)

This repo is currently set up for a bare-bones rover bring-up workflow:

- Drivetrain is `disarmed by default` on server startup.
- WebUI now uses a single `START / E-BRK / CLEAR` ignition button:
    - `START` appears when disarmed and prechecks pass.
    - `E-BRK` appears when started.
    - `CLEAR` appears when E-Brake is latched.
- `drive` commands are ignored unless drivetrain is started and not E-Braked.
- Tune dialog includes `Swap Front/Back` camera role mapping.
- Teensy telemetry is in bare-bones mode:
    - `curr` is active.
    - `volt`, `temp`, `enc_l`, `enc_r` are forced to `0` until wiring is added.

Verification now runs in CI before build:

- Script: `scripts/verify.sh`
- Workflow gate: `.github/workflows/build.yml` (`verify` job, then `build`)

---

## Repository Layout

```
rover/
├── pi_server/              # C++ daemon for the BTT Pi
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp        # Entry point + command dispatcher
│   │   ├── Config.hpp/.cpp # /etc/rover/rover.conf loader
│   │   ├── Protocol.hpp    # UUIDs, command strings, port numbers
│   │   ├── wifi/           # WebSocket server (POSIX + OpenSSL)
│   │   ├── webui/          # Embedded HTTP + WebSocket server
│   │   ├── camera/         # V4L2 capture → MJPEG HTTP server
│   │   ├── gpio/           # libgpiod output controller
│   │   ├── serial/         # USB serial bridge to Teensy
│   │   └── ota/            # Receive firmware chunks, flash Teensy
│   └── scripts/
│       ├── install.sh      # One-shot install + build script
│       ├── rover.service   # systemd unit file
│       └── rover.conf.example
│
├── teensy_firmware/         # PlatformIO project (Teensy 4.1)
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp         # Arduino loop + serial JSON protocol
│       ├── MotorController.hpp  # BTS7960 dual H-bridge driver
│       └── SensorHub.hpp    # Encoders + V/I/temp sensor aggregator
│
└── android_app/             # Android Kotlin app
    ├── app/src/main/
    │   ├── java/com/rover/app/
    │   │   ├── MainActivity.kt
    │   │   ├── RoverProtocol.kt   # Message builder + data classes
    │   │   ├── RoverViewModel.kt  # State manager + command router
    │   │   ├── ble/BleManager.kt
    │   │   ├── network/WebSocketManager.kt
    │   │   └── ui/
    │   │       ├── JoystickView.kt
    │   │       ├── ConnectionFragment.kt
    │   │       ├── ControllerFragment.kt
    │   │       ├── CameraFragment.kt  (+ MjpegView)
    │   │       └── OtaFragment.kt
    │   └── res/
    └── build.gradle.kts
```

---

## Quick Start

All builds are managed through the `ogun` CLI or `make`. Everything runs on Linux.

### 1 — First-time Pi Setup (run as root on the BTT Pi)

```bash
git clone <this-repo> /opt/rover-src
cd /opt/rover-src
chmod +x ogun
sudo ./ogun deps          # install all system dependencies
./ogun build pi            # build rover_server
sudo make install          # install binary + systemd service
```

**Edit `/etc/rover/rover.conf`** to match your GPIO wiring and camera devices.

### 2 — Verify the service

```bash
systemctl status rover
journalctl -u rover -f
```

### 3 — Teensy Firmware

```bash
./ogun build teensy        # compile .hex via PlatformIO
./ogun flash teensy        # flash via USB
```

Or use PlatformIO directly in VS Code.

### 4 — Dev Cycle (on the Pi)

```bash
./ogun build pi && sudo systemctl restart rover
```

### 5 — Dev Cycle (cross-compile from desktop)

```bash
./ogun sync-sysroot <pi-ip>   # one-time: pull ARM64 libs
./ogun build cross             # cross-compile for ARM64
./ogun deploy <pi-ip>          # package + deploy via SSH
```

### 6 — Full Build + Deploy

```bash
./ogun build all               # Pi server + Teensy firmware
./ogun deploy <pi-ip>          # package everything + deploy
```

### 7 — Monitor & Status

```bash
./ogun monitor serial          # watch Teensy serial output
./ogun monitor rover <pi-ip>   # tail rover logs on Pi
./ogun status <pi-ip>          # system + service health check
```

### 8 — Android App

Open `android_app/` in **Android Studio**, or:

```bash
./ogun build android           # debug APK via Gradle
```

### Build Targets Reference

| Command | Description |
|---------|-------------|
| `./ogun build all` | Build Pi server + Teensy firmware |
| `./ogun build pi` | Build `rover_server` natively |
| `./ogun build teensy` | Build Teensy `.hex` via PlatformIO |
| `./ogun build cross` | Cross-compile `rover_server` for ARM64 |
| `./ogun build android` | Build Android APK (debug) |
| `./ogun flash teensy` | Flash Teensy via USB |
| `./ogun package` | Create `rover-update.tar.gz` bundle |
| `./ogun deploy <ip>` | Package + deploy to Pi via SSH |
| `./ogun update` | Pull latest GitHub Release + apply (on Pi) |
| `./ogun update --flash-teensy` | Update + flash Teensy firmware |
| `./ogun autoupdate enable` | Enable scheduled background updates on Pi |
| `./ogun autoupdate status` | Show auto-update timer/service status |
| `./ogun release patch` | Bump version, git tag (you push) |
| `./ogun monitor serial` | Watch Teensy serial output |
| `./ogun monitor rover <ip>` | Tail rover service logs |
| `./ogun status <ip>` | Show Pi system + rover health |
| `./ogun clean [target]` | Remove build artifacts |
| `./ogun deps` | Install build deps (sudo, on Pi) |
| `./ogun info` | Show build environment |

Or use `make` directly: `make all`, `make pi`, `make teensy`, `make cross`, `make deploy PI=<ip>`, etc.

---

## Releases & OTA Updates

Builds are automated via **GitHub Actions**. When you push a version tag, CI will:
1. Cross-compile `rover_server` for ARM64 (the Pi's architecture)
2. Build `teensy_firmware.hex` via PlatformIO
3. Package both into `ogun-X.Y.Z-aarch64.tar.gz`
4. Publish a **GitHub Release** with the tarball attached

### Cutting a release

```bash
./ogun release patch          # bumps 0.1.0 → 0.1.1, creates git tag v0.1.1
git push origin main v0.1.1   # pushes code + tag → triggers CI
```

### Updating the Pi (from a GitHub Release)

SSH into the Pi:
```bash
sudo ./ogun update                 # pulls latest release, updates rover_server
sudo ./ogun update --flash-teensy  # also flashes the Teensy
```

Or for a specific version:
```bash
sudo ./ogun update --version v0.1.0 --flash-teensy
```

### Optional: Scheduled Auto-Updates on the Pi

After updating to a release that includes auto-update files, enable the timer:

```bash
sudo ./ogun autoupdate enable
```

Default schedule is every 1 hour (`rover-auto-update.timer`) and updates rover software without flashing Teensy.

```bash
sudo ./ogun autoupdate status
sudo ./ogun autoupdate run-now
sudo ./ogun autoupdate disable
```

The update flow:
```
GitHub Release (.tar.gz)
    │
    ↓  curl/wget
Pi: /tmp/ogun-update.XXXXXX/
    ├── rover_server          → /usr/local/bin/rover_server
    ├── teensy_firmware.hex   → teensy_loader_cli → Teensy USB
    ├── rover.service         → /etc/systemd/system/
    ├── rover.conf.example    → /etc/rover/ (only if no config exists)
    └── webui/                → /opt/rover/webui/
```

---

## Wiring Reference

### BTS7960 → Teensy 4.1

| BTS7960 Signal | Teensy Pin (Left) | Teensy Pin (Right) |
|----------------|-------------------|--------------------|
| RPWM           | 2                 | 5                  |
| LPWM           | 3                 | 6                  |
| R_EN / L_EN    | 4                 | 7                  |
| GND            | GND               | GND                |
| VCC (logic)    | 5 V               | 5 V                |

### Encoders → Teensy 4.1

| Signal | Left | Right |
|--------|------|-------|
| A      | 8    | 10    |
| B      | 9    | 11    |

### GPIO → BTT Pi (BCM numbers, edit `rover.conf` to change)

| Name    | BCM Pin |
|---------|---------|
| horn    | 17      |
| led_fwd | 27      |
| led_rev | 22      |
| aux1    | 23      |
| aux2    | 24      |

---

## Communication Protocol

### Phone ↔ Pi (JSON over WebSocket)

| Direction | Type | Payload keys |
|-----------|------|--------------|
| Phone → Pi | `drive` | `x`, `y`, `rot` (−1..1) |
| Phone → Pi | `ignition_start` | — |
| Phone → Pi | `estop_clear` | — |
| Phone → Pi | `gpio` | `pin`, `state` (bool) |
| Phone → Pi | `estop` | — |
| Phone → Pi | `audio` | `file` |
| Phone → Pi | `ota_begin` | `total` (chunk count) |
| Phone → Pi | `ota` | `chunk` (index), `data` (base64) |
| Phone → Pi | `cameras` | `enabled` (bool) |
| Phone → Pi | `sleep` | — |
| Phone → Pi | `wake` | — |
| Pi → Phone | `telemetry` | `enc_l`, `enc_r`, `volt`, `curr`, `temp`, `started`, `estop`, `precheck_ok` |
| Pi → Phone | `ota_prog` | `pct` (0-100), `msg` |
| Pi → Phone | `power` | `sleeping`, `cameras_on` |

### Pi ↔ Teensy (JSON over USB Serial 115200)

| Direction | Command | Keys |
|-----------|---------|------|
| Pi → Teensy | `drive` | `l`, `r` (−1..1) |
| Pi → Teensy | `stop` | — |
| Pi → Teensy | `sensor_req` | — |
| Pi → Teensy | `enc_reset` | — |
| Pi → Teensy | `arm` | — |
| Pi → Teensy | `disarm` | — |
| Pi → Teensy | `estop` | — |
| Pi → Teensy | `estop_clear` | — |
| Pi → Teensy | `bootloader` | — |
| Teensy → Pi | `sensors` | `enc_l`, `enc_r`, `volt`, `curr`, `temp` |

---

## Ports / Endpoints

| Service | Protocol | Default Port |
|---------|----------|-------------|
| Web UI | HTTP + WS | 8080 |
| Control | WebSocket | 9000 |
| Camera 0 | HTTP MJPEG | 8081 |
| Camera 1 | HTTP MJPEG | 8082 |

Web UI status endpoint:

- `GET /api/status` (note: not `/status`)

---

## Remote Teensy Flashing (OTA)

1. Compile your updated Teensy firmware in PlatformIO → produces `.hex`
2. In the Android app → **Controller** → **⬆ FW** button
3. Pick the `.hex` file
4. Tap **Flash Teensy** — the Pi receives chunks, assembles the hex, and calls `teensy_loader_cli`

The Teensy auto-reboots into bootloader mode when the Pi calls `teensy_loader_cli`.
No physical button press needed (Teensy 4.x supports auto-reboot via USB).

---

## Dependencies

### Pi (installed by `install.sh`)
- `build-essential`, `cmake`, `ninja-build`
- `libgpiod-dev`
- `libjpeg-dev`
- `libssl-dev` (OpenSSL for WebSocket SHA1)
- `teensy_loader_cli` (built from source)

### Teensy (PlatformIO)
- `paulstoffregen/Encoder`

### Android
- OkHttp 4 (WebSocket)
- AndroidX Navigation
- AndroidX Lifecycle / ViewModel
- Kotlin Coroutines
