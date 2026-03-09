# Ogun Handoff Notes (Mar 9, 2026)

Quick resume notes for next remote session.

## Last pushed commit

- `fb22d8b` on `main`
- Push succeeded to `origin/main`

## What was added

- CI verification gate before build:
  - `scripts/verify.sh`
  - `.github/workflows/build.yml` now has `verify` job and `build` depends on it.
- Teensy update/OTA flow improvements:
  - Uses `teensy_loader_cli -w -s -v` soft reboot mode.
  - Firmware supports `{"cmd":"bootloader"}` for self-upgrade entry.
- WebUI drivetrain control changes:
  - Single ignition button states: `START`, `E-BRK`, `CLEAR`.
  - Drivetrain defaults to disarmed; must start explicitly.
  - Drive commands are blocked unless started and not estop-latched.
- Tune dialog:
  - Added `Swap Front/Back` camera role control.
  - Camera role mapping persisted in browser local storage.
- Bare-bones telemetry mode in Teensy firmware:
  - Current sense kept.
  - Voltage/temp/encoder fields forced to 0 for now.

## Important behavior notes

- If rover does not move after restart, press `START` in WebUI first.
- If `CLEAR` is shown, E-Brake is latched; clear then press `START`.
- If `START` is disabled, pre-ignition checks are not passing (Teensy link/sleep state).
- Web status endpoint is `/api/status`.

## Files most recently touched

- `.github/workflows/build.yml`
- `scripts/verify.sh`
- `scripts/apply-update.sh`
- `pi_server/scripts/build-and-deploy.sh`
- `pi_server/scripts/install.sh`
- `pi_server/src/Protocol.hpp`
- `pi_server/src/main.cpp`
- `pi_server/src/ota/TeensyOta.cpp`
- `pi_server/webui/index.html`
- `teensy_firmware/src/main.cpp`
- `teensy_firmware/src/SensorHub.hpp`
- `README.md`

## Suggested first commands next session

```bash
cd /root/Ogun
./scripts/verify.sh

git status --short --branch
# if needed:
# git log --oneline -n 5
```

## If you want to publish a release

```bash
cd /root/Ogun
./ogun release patch
git push origin main --tags
```

Then on rover:

```bash
sudo ogun update --flash-teensy
```
