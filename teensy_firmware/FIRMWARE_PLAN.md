# Teensy Firmware Plan

## Current Baseline
- USB serial JSON protocol with `arm`, `drive`, `hb`, `stop`, `disarm`, `estop`, `estop_clear`,
  `sensor_req`, `enc_reset`, `pin_diag`, `pin_set`, `fw_cfg`, `fw_cfg_get`, `bootloader`.
- Dual BTS7960 tank-drive control.
- Encoder + voltage/current/temp telemetry.
- Safety watchdog (owner rulings 2026-10-07; rules in `src/SafetyLogic.hpp`, host test in
  `test/host/`):
  - Only operator frames feed it: `drive` and `hb` (`arm` opens the window). `sensor_req`,
    `stop` and `fw_cfg` do not.
  - No feed for `watchdog_ms` while armed: hard stop (PWM 0, enables LOW) and DISARM, latched
    until the next `arm`. No coast, whatever `ramp_sec` says. Emits
    `{"type":"safety","event":"watchdog","action":"disarm","trips":N,"watchdog_ms":W}`.
  - `watchdog_ms` is clamped to [100, 1000]; default 500.
  - Arming is always required: the boot state is disarmed, and `require_arm` is gone (the
    `fw_cfg` key is accepted and ignored; a `ROVER_REQUIRE_ARM=0` build is a compile error).
  - `arm` always starts from zero motor targets, and `estop` also disarms.
- `pin_set` writes only pins on the `pin_allow` list (default empty). Every BTS7960 pin (RPWM,
  LPWM, EN and IS current sense, runtime and compile-time) is refused always:
  `{"type":"pin_set","ok":false,"reason":"motor_pin"}`. Serial/SSH only for now (owner ruling
  2026-10-07); the Pi must not forward `pin_set` or `pin_allow` from network clients.
- Lines over 255 chars are dropped whole (never run as fragments). A line must hold exactly one
  `{`, so a partial line that ran into the next one is ignored.
- Numbers are strict: a value with no digits (`"x"`, `null`, a quoted number) or a non-finite
  float (`nan`, `inf`) is rejected and the old config value is kept; in a `drive` frame it reads
  as 0 (neutral).
- Built-in turn defaults (owner ruling 2026-10-07, from commit 573f8b8): `turn_max_pwm` 90,
  `turn_slowdown` 0.85, `turn_ramp_sec` 0.35.
- Host tests: `test/host/safety_logic_test.cpp` (the rules) and `test/host/firmware_sim_test.cpp`
  (the real `main.cpp` against `test/host/fake_arduino/`). Build commands are in each file's header.

## New Runtime Config Path
- Pi server reads Teensy firmware settings from `/etc/rover/rover.conf`.
- Pi server sends one serial command at startup: `{"cmd":"fw_cfg", ...}`.
- Teensy applies pin/calibration/timing settings immediately and replies with `{"type":"fw_cfg", ...}`.

Supported `fw_cfg` keys:
- Motor pins: `l_rpwm`, `l_lpwm`, `l_en`, `r_rpwm`, `r_lpwm`, `r_en`
- Encoder pins: `enc_la`, `enc_lb`, `enc_ra`, `enc_rb`
- Analog pins: `vbat_adc`, `curr_adc`, `temp_adc`
- Calibration: `vbat_div`, `curr_zero_mv`, `curr_sens_mv_per_a`
- Timing: `watchdog_ms` (clamped 100..1000), `telem_ms`
- `require_arm`: accepted and ignored (arming is always required).
- pin_set allowlist: `pin_allow` as a JSON array, e.g. `[30,31]`; `[]` clears it; a malformed
  list leaves it unchanged.

Readback command:
- `{"cmd":"fw_cfg_get"}` returns the active config as JSON, including `watchdog_trips` and
  `pin_allow`.

**Known limits (2026-10-07):** the Pi's full startup `fw_cfg` line is 515 chars, over the
Teensy's 255-char line limit, so it has never been applied (the shorter drive-tune `fw_cfg` is).
The `fw_cfg` reply is about 650 chars, over the Pi's 512-char receive limit, so it only reaches
a raw-serial reader. Send `pin_allow` in its own short `fw_cfg` line.

## Recommended Next Firmware Milestones
1. Add `fw_cfg_save` and `fw_cfg_load` with EEPROM/Flash persistence.
2. Add pin validation guardrails (disallow duplicate motor PWM pins and illegal ranges).
3. Add command ACK/NACK with error reasons for bad config payloads.
4. Add optional motor output ramp limiting to reduce current spikes.
5. Add telemetry field for watchdog trip count and uptime.
6. Add hardware-in-the-loop smoke test script for command/telemetry contract.

## Integration Notes
- Keep `teensy_push_fw_config=true` in `rover.conf` for centralized pin management.
- Use static defaults in `teensy_firmware/src/FirmwareConfig.hpp` as fallback.
- Changing Teensy pins no longer requires a firmware rebuild when using `fw_cfg`.
