# Teensy Firmware Plan

## Current Baseline
- USB serial JSON protocol with `drive`, `stop`, `sensor_req`, `enc_reset`.
- Dual BTS7960 tank-drive control.
- Encoder + voltage/current/temp telemetry.
- Safety watchdog stop on command timeout.

## New Runtime Config Path
- Pi server reads Teensy firmware settings from `/etc/rover/rover.conf`.
- Pi server sends one serial command at startup: `{"cmd":"fw_cfg", ...}`.
- Teensy applies pin/calibration/timing settings immediately and replies with `{"type":"fw_cfg", ...}`.

Supported `fw_cfg` keys:
- Motor pins: `l_rpwm`, `l_lpwm`, `l_en`, `r_rpwm`, `r_lpwm`, `r_en`
- Encoder pins: `enc_la`, `enc_lb`, `enc_ra`, `enc_rb`
- Analog pins: `vbat_adc`, `curr_adc`, `temp_adc`
- Calibration: `vbat_div`, `curr_zero_mv`, `curr_sens_mv_per_a`
- Timing: `watchdog_ms`, `telem_ms`

Readback command:
- `{"cmd":"fw_cfg_get"}` returns the active config as JSON.

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
