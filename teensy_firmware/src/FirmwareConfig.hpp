#pragma once

#include <Arduino.h>

namespace fwcfg {

// ---- Motor pins (BTS7960, Teensy 4.1 terminal breakout) -----
// RPWM/LPWM MUST be on PWM-capable pins for proportional speed control.
// Teensy 4.1 PWM pins: 0-15, 18, 19, 22-25, 28, 29, 33, 36, 37, 42-47
// Pins 16,17,20,21 do NOT have PWM — used for EN and IS only.
//
// Wiring (swap within each 4-pin group):
// Left:  RPWM=22(PWM), LPWM=23(PWM), EN=20(digital), IS=21(A7)
// Right: RPWM=18(PWM), LPWM=19(PWM), EN=16(digital), IS=17(A3)
// Turn:  RPWM=24(PWM), LPWM=25(PWM), EN=26(digital), IS=27(A13)
#ifndef ROVER_L_RPWM_PIN
#define ROVER_L_RPWM_PIN 22    // FlexPWM4 — PWM capable
#endif
#ifndef ROVER_L_LPWM_PIN
#define ROVER_L_LPWM_PIN 23    // FlexPWM4 — PWM capable
#endif
#ifndef ROVER_L_EN_PIN
#define ROVER_L_EN_PIN 20      // digital only
#endif

#ifndef ROVER_R_RPWM_PIN
#define ROVER_R_RPWM_PIN 18    // QuadTimer3 — PWM capable
#endif
#ifndef ROVER_R_LPWM_PIN
#define ROVER_R_LPWM_PIN 19    // QuadTimer3 — PWM capable
#endif
#ifndef ROVER_R_EN_PIN
#define ROVER_R_EN_PIN 16      // digital only
#endif

#ifndef ROVER_T_RPWM_PIN
#define ROVER_T_RPWM_PIN 24    // FlexPWM1 — PWM capable
#endif
#ifndef ROVER_T_LPWM_PIN
#define ROVER_T_LPWM_PIN 25    // FlexPWM1 — PWM capable
#endif
#ifndef ROVER_T_EN_PIN
#define ROVER_T_EN_PIN 26      // digital only
#endif

// ---- Encoder pins (not wired yet — parked on unused pins) --
#ifndef ROVER_ENC_LA_PIN
#define ROVER_ENC_LA_PIN 2
#endif
#ifndef ROVER_ENC_LB_PIN
#define ROVER_ENC_LB_PIN 3
#endif
#ifndef ROVER_ENC_RA_PIN
#define ROVER_ENC_RA_PIN 4
#endif
#ifndef ROVER_ENC_RB_PIN
#define ROVER_ENC_RB_PIN 5
#endif

// ---- Analog sensing pins -----------------------------------
// Voltage/temp: not wired, reads disabled in SensorHub.
#ifndef ROVER_VBAT_ADC_PIN
#define ROVER_VBAT_ADC_PIN A0   // pin 14 — not wired
#endif
#ifndef ROVER_TEMP_ADC_PIN
#define ROVER_TEMP_ADC_PIN A1   // pin 15 — not wired
#endif

// ---- BTS7960 current-sense IS pins -------------------------
// Left IS → pin 21 (A7), Right IS → pin 17 (A3), Turn IS → pin 27 (A13)
#ifndef ROVER_CURR_L_ADC_PIN
#define ROVER_CURR_L_ADC_PIN A7   // pin 21
#endif
#ifndef ROVER_CURR_R_ADC_PIN
#define ROVER_CURR_R_ADC_PIN A3   // pin 17
#endif
#ifndef ROVER_CURR_T_ADC_PIN
#define ROVER_CURR_T_ADC_PIN A13  // pin 27
#endif

// ---- Calibration -------------------------------------------
// BTS7960 IS output: ~8.5 mA per amp into a sense resistor.
// With typical 1kΩ to GND → ~8.5 mV/A.
// Adjust these once you measure your actual sense resistor.
#ifndef ROVER_VBAT_DIV_RATIO
#define ROVER_VBAT_DIV_RATIO 4.03f
#endif
#ifndef ROVER_CURR_ZERO_MV
#define ROVER_CURR_ZERO_MV 0.0f
#endif
#ifndef ROVER_CURR_SENS_MV_PER_A
#define ROVER_CURR_SENS_MV_PER_A 8.5f
#endif

// ---- Timing -------------------------------------------------
#ifndef ROVER_WATCHDOG_MS
#define ROVER_WATCHDOG_MS 500
#endif
#ifndef ROVER_TELEM_INTERVAL_MS
#define ROVER_TELEM_INTERVAL_MS 100
#endif

// ---- Drive behavior tuning (simple model) -------------------
#ifndef ROVER_MAX_PWM
#define ROVER_MAX_PWM 255
#endif
#ifndef ROVER_MIN_PWM
#define ROVER_MIN_PWM 0
#endif
#ifndef ROVER_RAMP_SEC
#define ROVER_RAMP_SEC 1.0f
#endif
#ifndef ROVER_TURN_MAX_PWM
#define ROVER_TURN_MAX_PWM 255
#endif
#ifndef ROVER_INVERT_TURN
#define ROVER_INVERT_TURN 0
#endif
// (removed — replaced by ROVER_RAMP_SEC above)

// ---- Safety -------------------------------------------------
// Low-voltage cutoff: set to 0.0 to disable (BMS handles cutoff).
// Only enable if you have a voltage divider wired to the VBAT ADC pin.
#ifndef ROVER_LOW_VOLTAGE_CUTOFF
#define ROVER_LOW_VOLTAGE_CUTOFF 0.0f
#endif
#ifndef ROVER_LOW_VOLTAGE_RESUME
#define ROVER_LOW_VOLTAGE_RESUME 0.0f
#endif
#ifndef ROVER_INPUT_DEADBAND
#define ROVER_INPUT_DEADBAND 0.05f
#endif
#ifndef ROVER_REQUIRE_ARM
#define ROVER_REQUIRE_ARM 1
#endif

constexpr uint8_t L_RPWM = ROVER_L_RPWM_PIN;
constexpr uint8_t L_LPWM = ROVER_L_LPWM_PIN;
constexpr uint8_t L_EN   = ROVER_L_EN_PIN;

constexpr uint8_t R_RPWM = ROVER_R_RPWM_PIN;
constexpr uint8_t R_LPWM = ROVER_R_LPWM_PIN;
constexpr uint8_t R_EN   = ROVER_R_EN_PIN;

constexpr uint8_t T_RPWM = ROVER_T_RPWM_PIN;
constexpr uint8_t T_LPWM = ROVER_T_LPWM_PIN;
constexpr uint8_t T_EN   = ROVER_T_EN_PIN;

constexpr uint8_t ENC_LA = ROVER_ENC_LA_PIN;
constexpr uint8_t ENC_LB = ROVER_ENC_LB_PIN;
constexpr uint8_t ENC_RA = ROVER_ENC_RA_PIN;
constexpr uint8_t ENC_RB = ROVER_ENC_RB_PIN;

constexpr uint8_t VBAT_ADC_PIN   = ROVER_VBAT_ADC_PIN;
constexpr uint8_t CURR_L_ADC_PIN = ROVER_CURR_L_ADC_PIN;
constexpr uint8_t CURR_R_ADC_PIN = ROVER_CURR_R_ADC_PIN;
constexpr uint8_t CURR_T_ADC_PIN = ROVER_CURR_T_ADC_PIN;
constexpr uint8_t TEMP_ADC_PIN   = ROVER_TEMP_ADC_PIN;

constexpr float VBAT_DIV_RATIO      = ROVER_VBAT_DIV_RATIO;
constexpr float CURR_ZERO_MV        = ROVER_CURR_ZERO_MV;
constexpr float CURR_SENS_MV_PER_A  = ROVER_CURR_SENS_MV_PER_A;

constexpr uint32_t WATCHDOG_MS       = ROVER_WATCHDOG_MS;
constexpr uint32_t TELEM_INTERVAL_MS = ROVER_TELEM_INTERVAL_MS;

constexpr uint8_t MAX_PWM       = ROVER_MAX_PWM;
constexpr uint8_t MIN_PWM       = ROVER_MIN_PWM;
constexpr float   RAMP_SEC      = ROVER_RAMP_SEC;
constexpr uint8_t TURN_MAX_PWM  = ROVER_TURN_MAX_PWM;
constexpr bool    INVERT_TURN   = ROVER_INVERT_TURN;

constexpr float LOW_VOLTAGE_CUTOFF = ROVER_LOW_VOLTAGE_CUTOFF;
constexpr float LOW_VOLTAGE_RESUME = ROVER_LOW_VOLTAGE_RESUME;
constexpr float INPUT_DEADBAND     = ROVER_INPUT_DEADBAND;
constexpr bool  REQUIRE_ARM        = ROVER_REQUIRE_ARM;

} // namespace fwcfg
