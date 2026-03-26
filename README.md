# Rover Control System

A full-stack rover platform built on top of a **BTT Pi 1.2** (Klipper board repurposed),
using a **Teensy 4.1** (terminal breakout) as a motor/sensor hub, **BTS7960** H-bridge motor drivers,
controlled from a **Web UI** over **WiFi/WebSocket**.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Web UI (Browser :8080)                      │
│  ┌──────────────┐  ┌─────────────────────┐                     │
│  │ WebSocket    │  │  Joystick + Tune +   │                     │
│  │  Client      │  │  Camera MJPEG UI    │                     │
│  └──────┬───────┘  └─────────────────────┘                     │
└─────────┼───────────────────────────────────────────────────────┘
             │ WebSocket (:9000 + :8080/ws)
             │ MJPEG   (:8081 / :8082)
┌─────────┼───────────────────────────────────────────────────────┐
│         │   BTT Pi 1.2                                          │
│  ┌──────┴──────────────────────────────────────────────────┐   │
│  │               rover_server (C++20)                       │   │
│  │  WifiServer  │  WebUiServer  │  CameraStream ×2          │   │
│  │  (WebSocket) │  (HTTP + WS)  │  (V4L2 + MJPEG HTTP)      │   │
│  │  GpioController (libgpiod)   │  TeensyOta (USB flash)     │   │
│  │──────────────────────────────────────────────────────────│   │
│  │  ModuleRegistry → loads src/modules/mod_*/               │   │
│  │  Each module: own .conf, loader.cpp, lifecycle hooks      │   │
│  └─────────────────────────────┬───────────────────────────┘   │
│                                 │ USB Serial (115200 baud)       │
└─────────────────────────────────┼───────────────────────────────┘
                                  │
                    ┌─────────────┴──────────────┐
                    │     Teensy 4.1 (terminal)   │
                    │  main.cpp (PlatformIO)       │
                    │  MotorController (BTS7960)  │
                    │  SensorHub (triple current) │
                    └─────────────────────────────┘
                      |         |         |
                 BTS7960 L  BTS7960 R  BTS7960 T
                    │           │         │
                Left Motor  Right Motor  Turn Motor
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
Ogun/
├── pi_server/                    # C++ daemon for the BTT Pi
│   ├── CMakeLists.txt
│   ├── modules/                  # .conf.example files for each module
│   │   └── mod_lights.conf.example
│   ├── src/
│   │   ├── main.cpp              # Entry point + command dispatcher
│   │   ├── Config.hpp/.cpp       # /etc/rover/rover.conf loader
│   │   ├── Protocol.hpp          # Command strings, UUIDs, ports
│   │   ├── module/               # Module framework
│   │   │   ├── RoverModule.hpp   # Base class + ModuleContext
│   │   │   ├── ModuleRegistry.*  # Static auto-registration
│   │   │   └── ModuleConf.*      # Per-module .conf loader
│   │   ├── modules/              # Drop-in modules (auto-discovered)
│   │   │   └── mod_lights/       # Example: GPIO headlights/taillights
│   │   │       ├── ModLights.hpp/.cpp
│   │   │       └── mod_lights_loader.cpp
│   │   ├── wifi/                 # WebSocket server (POSIX + OpenSSL)
│   │   ├── webui/                # Embedded HTTP + WebSocket server
│   │   ├── camera/               # V4L2 capture → MJPEG HTTP server
│   │   ├── gpio/                 # libgpiod output controller
│   │   ├── serial/               # USB serial bridge to Teensy
│   │   └── ota/                  # Receive firmware chunks, flash Teensy
│   ├── webui/
│   │   └── index.html            # Single-page control UI
│   └── scripts/
│       ├── install.sh
│       ├── rover.service         # systemd unit (Type=notify, watchdog)
│       └── rover.conf.example
│
├── teensy_firmware/               # PlatformIO project (Teensy 4.1)
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp               # Arduino loop + serial JSON protocol
│       ├── FirmwareConfig.hpp     # Runtime pin/tuning config receiver
│       ├── MotorController.hpp    # BTS7960 H-bridge (setTarget/tick/coast + turn)
│       └── SensorHub.hpp          # Triple current sense + telemetry
│
├── scripts/                       # Update, verify, auto-update scripts
├── ogun                           # CLI tool for build/deploy/release
└── VERSION                        # Single source of truth for version
```

---

## Quick Start

All builds are managed through the `ogun` CLI or `make`. Everything runs on Linux.

### Recommended Workflow for BTT Pi (1 GB RAM)

- Treat the Pi as a deploy/update target, not a primary build machine.
- Build/release through GitHub Actions (`git push` + version tag), then run `ogun update` on the Pi.
- If you must build on the Pi, use safe mode (`./ogun build pi --safe`) to force single-job compilation.

### 1 — First-time Pi Setup (run as root on the BTT Pi)

```bash
git clone <this-repo> /opt/rover-src
cd /opt/rover-src
chmod +x ogun
sudo ./ogun deps          # install all system dependencies
./ogun build pi --safe     # build rover_server (single-job mode on Pi)
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
./ogun build pi --safe && sudo systemctl restart rover
```

For normal development, prefer the cross/CI flow below to avoid RAM pressure on-device.

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
./ogun log teensy              # watch Teensy serial output
./ogun log rover <pi-ip>       # tail rover logs on Pi
./ogun check system <pi-ip>    # system + service health check
```

Legacy aliases still work: `monitor ...` and `status ...`.

### 8 — Web UI

Control the rover through the web UI at `http://<pi-ip>:8080`.

### Build Targets Reference

| Command | Description |
|---------|-------------|
| `./ogun build all` | Build Pi server + Teensy firmware |
| `./ogun build pi --safe` | Build `rover_server` natively with low-RAM-safe settings |
| `./ogun build teensy` | Build Teensy `.hex` via PlatformIO |
| `./ogun build cross` | Cross-compile `rover_server` for ARM64 |
| `./ogun flash teensy` | Flash Teensy via USB |
| `./ogun package` | Create `rover-update.tar.gz` bundle |
| `./ogun deploy <ip>` | Package + deploy to Pi via SSH |
| `./ogun update system` | Pull latest GitHub Release and update rover software |
| `./ogun update teensy` | Update and flash Teensy firmware |
| `./ogun update all` | Update rover software + Teensy firmware |
| `./ogun enable autoupdate` | Enable scheduled background updates on Pi |
| `./ogun check autoupdate` | Show auto-update timer/service status |
| `./ogun release patch` | Bump version, git tag (you push) |
| `./ogun log teensy` | Watch Teensy serial output |
| `./ogun log rover <ip>` | Tail rover service logs |
| `./ogun check system <ip>` | Show Pi system + rover health |
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
./ogun release patch          # bumps patch and creates git tag
git push origin main <tag>    # pushes code + tag → triggers CI
```

### Updating the Pi (from a GitHub Release)

SSH into the Pi:
```bash
sudo ./ogun update system   # pulls latest release and updates rover software
sudo ./ogun update teensy   # update + flash Teensy
sudo ./ogun update all      # same as teensy (full update)
```

Or for a specific version:
```bash
sudo ./ogun update all --version v0.9.0
```

### Optional: Scheduled Auto-Updates on the Pi

After updating to a release that includes auto-update files, enable the timer:

```bash
sudo ./ogun enable autoupdate
```

Default schedule is every 1 hour (`rover-auto-update.timer`) and updates rover software without flashing Teensy.

```bash
sudo ./ogun check autoupdate
sudo ./ogun autoupdate run-now
sudo ./ogun disable autoupdate
```

The update flow:
```
GitHub Release (.tar.gz)
    │
    ↓  streamed curl/wget extraction
Pi: /tmp/ogun-update.XXXXXX/
    ├── rover_server          → /usr/local/bin/rover_server
    ├── teensy_firmware.hex   → teensy_loader_cli → Teensy USB
    ├── rover.service         → /etc/systemd/system/
    ├── rover.conf.example    → /etc/rover/ (only if no config exists)
    └── webui/                → /opt/rover/webui/
```

---

## Wiring Reference

### BTS7960 → Teensy 4.1 (Terminal Breakout)

| BTS7960 Signal | Teensy Pin (Left) | Teensy Pin (Right) | Teensy Pin (Turn) |
|----------------|-------------------|--------------------|-------------------|
| RPWM           | 20                | 16                 | 24                |
| LPWM           | 21                | 17                 | 25                |
| R_EN / L_EN    | 22                | 18                 | 26                |
| GND            | GND               | GND                | GND               |
| VCC (logic)    | 5 V               | 5 V                | 5 V               |

### Encoders → Teensy 4.1 (not wired yet)

| Signal | Left | Right |
|--------|------|-------|
| A      | 2    | 4     |
| B      | 3    | 5     |

### Current Sense → Teensy 4.1

| Sensor | Teensy ADC Pin |
|--------|----------------|
| Left IS (BTS7960) | 23 |
| Right IS (BTS7960) | 19 |
| Turn IS (BTS7960) | 27 |

### GPIO → BTT Pi (BCM numbers)

No GPIO pins are wired in the base config. Modules can claim pins via their
own `.conf` files (see **Modules** below).

---

## Communication Protocol

### Phone ↔ Pi (JSON over WebSocket)

| Direction | Type | Payload keys |
|-----------|------|--------------|
| Phone → Pi | `drive` | `x`, `y`, `rot` (−1..1) — `y` drives, `rot` turns |
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
| Phone → Pi | `drive_tune` | `max_pwm`, `min_pwm`, `ramp_sec`, `invert_left`, `invert_right` |
| Phone → Pi | `drive_tune_save` | same as above + persists to rover.conf |
| Phone → Pi | `update_check` | — |
| Phone → Pi | `reboot` | — |
| Pi → Phone | `telemetry` | `enc_l`, `enc_r`, `volt`, `curr_l`, `curr_r`, `curr_t`, `temp`, `started`, `estop`, `precheck_ok`, `teensy_connected` |
| Pi → Phone | `drive_tune` | `ok`, `saved`, `max_pwm`, `min_pwm`, `ramp_sec`, … |
| Pi → Phone | `ota_prog` | `pct` (0-100), `msg` |
| Pi → Phone | `power` | `sleeping`, `cameras_on` |
| Pi → Phone | `update` | `status`, `detail` |

### Pi ↔ Teensy (JSON over USB Serial 115200)

| Direction | Command | Keys |
|-----------|---------|------|
| Pi → Teensy | `drive` | `l`, `r` (−1..1), `t` (−1..1 turn) |
| Pi → Teensy | `stop` | — |
| Pi → Teensy | `sensor_req` | — |
| Pi → Teensy | `enc_reset` | — |
| Pi → Teensy | `arm` | — |
| Pi → Teensy | `disarm` | — |
| Pi → Teensy | `estop` | — |
| Pi → Teensy | `estop_clear` | — |
| Pi → Teensy | `bootloader` | — |
| Pi → Teensy | `fw_cfg` | pin map, tuning, watchdog, telem interval |
| Teensy → Pi | `sensors` | `enc_l`, `enc_r`, `volt`, `curr_l`, `curr_r`, `curr_t`, `temp` |

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
2. In the Web UI → **OTA** panel → upload the `.hex` file
3. Tap **Flash Teensy** — the Pi receives chunks, assembles the hex, and calls `teensy_loader_cli`

The Teensy auto-reboots into bootloader mode when the Pi calls `teensy_loader_cli`.
No physical button press needed (Teensy 4.x supports auto-reboot via USB).

---

## Modules

The rover uses an **AzerothCore-style module loader**. Each module lives in its
own folder under `pi_server/src/modules/mod_<name>/` and registers itself via a
static initializer — no manual wiring needed.

### Module structure

```
pi_server/src/modules/mod_<name>/
├── Mod<Name>.hpp          # Header — inherits RoverModule
├── Mod<Name>.cpp          # Implementation
└── mod_<name>_loader.cpp  # Auto-registration (1 line + include)
```

A matching config file goes in `/etc/rover/modules/<name>.conf` on the Pi
(example templates ship in `pi_server/modules/`).

### Creating a new module

1. **Create the folder**: `pi_server/src/modules/mod_myfeature/`

2. **Write the class** (inherit `RoverModule`):
   ```cpp
   #include "module/RoverModule.hpp"
   class ModMyFeature : public RoverModule {
   public:
       const char* name() const override { return "myfeature"; }
       bool onLoad(const std::map<std::string,std::string>& conf,
                   const ModuleContext& ctx) override;
       void onShutdown() override;
       bool onCommand(const std::string& type,
                      const std::string& json) override;
       void onTick() override;  // called every telemetry cycle
   };
   ```

3. **Write the loader** (`mod_myfeature_loader.cpp`):
   ```cpp
   #include "module/ModuleRegistry.hpp"
   #include "modules/mod_myfeature/ModMyFeature.hpp"
   static bool _reg = ModuleRegistry::add([]() {
       return std::make_unique<ModMyFeature>();
   });
   ```

4. **Build** — CMake auto-discovers `src/modules/*/*.cpp`, no edits needed.

5. **Deploy config** — copy your `.conf.example` to `/etc/rover/modules/<name>.conf`.

### Lifecycle hooks

| Hook | When | Notes |
|------|------|-------|
| `onLoad(conf, ctx)` | Server startup | Receives parsed conf + broadcast/teensy/gpio access. Return `false` to skip. |
| `onCommand(type, json)` | WS message arrives | Return `true` if this module handled the command. |
| `onTick()` | Every telemetry cycle (~100 ms) | For periodic polling or state updates. |
| `onShutdown()` | Clean server exit | Turn off outputs, release resources. |

### Included example: `mod_lights`

GPIO-driven headlights/taillights. Config (`/etc/rover/modules/lights.conf`):
```
headlight_pin = 17
taillight_pin = 27
```
WS commands: `{"type":"lights","headlights":true}`, `{"type":"lights_status"}`

---

## Dependencies

### Pi (installed by `install.sh` / `ogun deps`)
- `build-essential`, `cmake`, `ninja-build`
- `libgpiod-dev`
- `libjpeg-dev`
- `libssl-dev` (OpenSSL for WebSocket SHA1)
- `libsystemd-dev` (sd_notify for watchdog)
- `teensy_loader_cli` (built from source)

### Teensy (PlatformIO)
- `paulstoffregen/Encoder`

---

## Planned Features

Upcoming work, roughly in priority order. Each will be implemented as a
drop-in module or core enhancement.

### GPS Telemetry (`mod_gps`)
- USB or UART GPS module (e.g. u-blox NEO-6M/M8N)
- Parse NMEA sentences, broadcast lat/lon/speed/heading in telemetry
- WebUI map overlay (Leaflet.js or similar) with live position pin
- Optional geofence — auto-ESTOP if rover leaves a bounding box

### Bluetooth Control (`mod_ble_control`)
- BLE GATT control channel (UUIDs already defined in Protocol.hpp)
- Allow driving via BLE gamepad or phone app without WiFi
- Dual-transport: BLE and WiFi commands feed the same dispatcher
- Range fallback: BLE takes over when WiFi drops

### Broadband / Low-Bandwidth Cellular Support
- WebRTC or adaptive-bitrate MJPEG for control over LTE/5G
- Compressed command channel (binary protocol or CBOR over WebSocket)
- Latency-aware drive — auto-reduce max speed when round-trip exceeds threshold
- Optional TURN/STUN relay for NAT traversal (drive from anywhere)

### Mirror / Pan-Tilt Cameras (`mod_servo_mirror`)
- Servo-driven pan/tilt mount for one or both cameras
- WS commands: `{"type":"mirror","pan":90,"tilt":45}`
- WebUI slider or drag-to-look control
- Preset positions (forward, left-check, right-check, rear)

### Sound Effects (`mod_audio`)
- Trigger WAV/MP3 playback from WebUI buttons or module commands
- Configurable sound bank in `/opt/rover/sounds/`
- Horn, engine idle, reverse beep, custom clips
- Volume control via WS command

### Enhanced Web UI
- Dark/light theme toggle
- Resizable camera feeds with drag-to-swap
- Telemetry graphs (voltage, current, temperature over time)
- Module-contributed UI panels — modules can register HTML fragments
- Mobile-optimized layout (single-column, larger touch targets)
- Connection quality indicator (latency bar + signal strength)
