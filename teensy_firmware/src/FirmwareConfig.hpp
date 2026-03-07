#pragma once

#include <Arduino.h>

namespace fwcfg {

// ---- Motor pins (BTS7960) ---------------------------------
#ifndef ROVER_L_RPWM_PIN
#define ROVER_L_RPWM_PIN 21
#endif
#ifndef ROVER_L_LPWM_PIN
#define ROVER_L_LPWM_PIN 19
#endif
#ifndef ROVER_L_EN_PIN
#define ROVER_L_EN_PIN 18
#endif

#ifndef ROVER_R_RPWM_PIN
#define ROVER_R_RPWM_PIN 22
#endif
#ifndef ROVER_R_LPWM_PIN
#define ROVER_R_LPWM_PIN 23
#endif
#ifndef ROVER_R_EN_PIN
#define ROVER_R_EN_PIN 16
#endif

// ---- Encoder pins ------------------------------------------
#ifndef ROVER_ENC_LA_PIN
#define ROVER_ENC_LA_PIN 8
#endif
#ifndef ROVER_ENC_LB_PIN
#define ROVER_ENC_LB_PIN 9
#endif
#ifndef ROVER_ENC_RA_PIN
#define ROVER_ENC_RA_PIN 10
#endif
#ifndef ROVER_ENC_RB_PIN
#define ROVER_ENC_RB_PIN 11
#endif

// ---- Analog sensing pins -----------------------------------
#ifndef ROVER_VBAT_ADC_PIN
#define ROVER_VBAT_ADC_PIN A0
#endif
#ifndef ROVER_CURR_ADC_PIN
#define ROVER_CURR_ADC_PIN 17
#endif
#ifndef ROVER_TEMP_ADC_PIN
#define ROVER_TEMP_ADC_PIN 20
#endif

// ---- Calibration -------------------------------------------
#ifndef ROVER_VBAT_DIV_RATIO
#define ROVER_VBAT_DIV_RATIO 4.03f
#endif
#ifndef ROVER_CURR_ZERO_MV
#define ROVER_CURR_ZERO_MV 1650.0f
#endif
#ifndef ROVER_CURR_SENS_MV_PER_A
#define ROVER_CURR_SENS_MV_PER_A 66.0f
#endif

// ---- Timing -------------------------------------------------
#ifndef ROVER_WATCHDOG_MS
#define ROVER_WATCHDOG_MS 250
#endif
#ifndef ROVER_TELEM_INTERVAL_MS
#define ROVER_TELEM_INTERVAL_MS 100
#endif

// ---- Drive behavior tuning ----------------------------------
#ifndef ROVER_DRIVE_MAX_FWD
#define ROVER_DRIVE_MAX_FWD 0.45f
#endif
#ifndef ROVER_DRIVE_MAX_REV
#define ROVER_DRIVE_MAX_REV 0.35f
#endif
#ifndef ROVER_TURN_MAX
#define ROVER_TURN_MAX 0.50f
#endif
#ifndef ROVER_THROTTLE_EXPO
#define ROVER_THROTTLE_EXPO 2.0f
#endif
#ifndef ROVER_TURN_EXPO
#define ROVER_TURN_EXPO 1.5f
#endif
#ifndef ROVER_ACCEL_UP_PER_S
#define ROVER_ACCEL_UP_PER_S 1.2f
#endif
#ifndef ROVER_ACCEL_DOWN_PER_S
#define ROVER_ACCEL_DOWN_PER_S 8.0f
#endif

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

constexpr uint8_t ENC_LA = ROVER_ENC_LA_PIN;
constexpr uint8_t ENC_LB = ROVER_ENC_LB_PIN;
constexpr uint8_t ENC_RA = ROVER_ENC_RA_PIN;
constexpr uint8_t ENC_RB = ROVER_ENC_RB_PIN;

constexpr uint8_t VBAT_ADC_PIN = ROVER_VBAT_ADC_PIN;
constexpr uint8_t CURR_ADC_PIN = ROVER_CURR_ADC_PIN;
constexpr uint8_t TEMP_ADC_PIN = ROVER_TEMP_ADC_PIN;

constexpr float VBAT_DIV_RATIO      = ROVER_VBAT_DIV_RATIO;
constexpr float CURR_ZERO_MV        = ROVER_CURR_ZERO_MV;
constexpr float CURR_SENS_MV_PER_A  = ROVER_CURR_SENS_MV_PER_A;

constexpr uint32_t WATCHDOG_MS       = ROVER_WATCHDOG_MS;
constexpr uint32_t TELEM_INTERVAL_MS = ROVER_TELEM_INTERVAL_MS;

constexpr float DRIVE_MAX_FWD   = ROVER_DRIVE_MAX_FWD;
constexpr float DRIVE_MAX_REV   = ROVER_DRIVE_MAX_REV;
constexpr float TURN_MAX        = ROVER_TURN_MAX;
constexpr float THROTTLE_EXPO   = ROVER_THROTTLE_EXPO;
constexpr float TURN_EXPO       = ROVER_TURN_EXPO;
constexpr float ACCEL_UP_PER_S  = ROVER_ACCEL_UP_PER_S;
constexpr float ACCEL_DOWN_PER_S = ROVER_ACCEL_DOWN_PER_S;

constexpr float LOW_VOLTAGE_CUTOFF = ROVER_LOW_VOLTAGE_CUTOFF;
constexpr float LOW_VOLTAGE_RESUME = ROVER_LOW_VOLTAGE_RESUME;
constexpr float INPUT_DEADBAND     = ROVER_INPUT_DEADBAND;
constexpr bool  REQUIRE_ARM        = ROVER_REQUIRE_ARM;

} // namespace fwcfg
