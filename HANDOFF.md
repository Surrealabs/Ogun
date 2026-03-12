# Ogun Handoff Notes (Mar 12, 2026)

Quick resume notes for next remote session.

## Current version

- `v0.11.0` on `main`
- Pushed to `origin/main` — CI should have built it

## What's new since last handoff

### v0.9.7 – v0.10.2 (prior sessions)
- Simplified tuning model (5 controls: invert L/R, max/min PWM, ramp sec)
- Serial write mutex + gentle coast watchdog (fixed motor stalls)
- Sliding-window average for telemetry smoothing (10 samples)
- Robust Teensy reconnect (auto-reconnect via rxThread)
- Pi reboot from WebUI + NO LINK pulsing indicator
- systemd Type=notify + sd_notify READY=1 (fixed watchdog killing server)
- rxThread always starts (even if Teensy missing at boot)

### v0.10.3 – Persistent tune
- Server caches latest tune JSON, sends to new WS clients on connect
- Initial tune seeded from rover.conf at startup
- Merged Apply + Save Persistent into single "Save" button

### v0.11.0 – Module system (AzerothCore-style)
- `RoverModule` base class with lifecycle hooks
- `ModuleRegistry`: static auto-registration via loader.cpp files
- Per-module `.conf` files in `/etc/rover/modules/`
- CMake auto-discovers `src/modules/*/*.cpp`
- `dispatchCommand` returns bool; unhandled commands route to modules
- Example `mod_lights` module (GPIO headlights/taillights)

## Important behavior notes

- Drivetrain is **disarmed by default** on startup.
- Press `START` in WebUI to arm. `E-BRK` to stop. `CLEAR` to unlatch.
- Tune settings persist to rover.conf via the "Save" button.
- Modules load from `/etc/rover/modules/<name>.conf` — no conf = module is inactive.
- systemd service: `Type=notify`, `WatchdogSec=30`, `Restart=always`.

## Files most recently touched

- `pi_server/CMakeLists.txt` (module glob added)
- `pi_server/src/main.cpp` (module integration, dispatchCommand returns bool)
- `pi_server/src/module/` (new: RoverModule, ModuleRegistry, ModuleConf)
- `pi_server/src/modules/mod_lights/` (new: example module)
- `pi_server/modules/mod_lights.conf.example`
- `pi_server/webui/index.html` (merged tune buttons)
- `pi_server/src/webui/WebUiServer.hpp/.cpp` (tune caching)
- `README.md`

## Suggested first commands next session

```bash
cd /root/Ogun
git log --oneline -n 10
cat VERSION
```

## Publishing a release

```bash
cd /root/Ogun
./ogun release patch
git push origin main --tags
```

Then on rover:

```bash
sudo ogun update --version vX.Y.Z
```

## Creating a new module

```bash
mkdir -p pi_server/src/modules/mod_myfeature
# Create: ModMyFeature.hpp, ModMyFeature.cpp, mod_myfeature_loader.cpp
# Create: pi_server/modules/mod_myfeature.conf.example
# Deploy conf to: /etc/rover/modules/myfeature.conf
# CMake auto-discovers — no edits needed
```
