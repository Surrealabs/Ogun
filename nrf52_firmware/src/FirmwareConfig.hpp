#pragma once
// ============================================================
//  FirmwareConfig — nRF52840 pin assignments & tuning defaults
//
//  Adafruit Feather nRF52840 pin mapping.
//  Adjust these to match your actual wiring.
// ============================================================
#include <Arduino.h>

namespace fwcfg {

// ---- Motor pins (BTS7960) ---------------------------------
// These map to Feather nRF52840 digital pins.
// Adjust to your wiring — these are reasonable defaults.
#ifndef ROVER_L_RPWM_PIN
#define ROVER_L_RPWM_PIN 5
#endif
#ifndef ROVER_L_LPWM_PIN
#define ROVER_L_LPWM_PIN 6
#endif
#ifndef ROVER_L_EN_PIN
#define ROVER_L_EN_PIN 9
#endif

#ifndef ROVER_R_RPWM_PIN
#define ROVER_R_RPWM_PIN 10
#endif
#ifndef ROVER_R_LPWM_PIN
#define ROVER_R_LPWM_PIN 11
#endif
#ifndef ROVER_R_EN_PIN
#define ROVER_R_EN_PIN 12
#endif

// ---- Encoder pins (optional — interrupt-based on nRF52) ----
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
#define ROVER_ENC_RB_PIN 7
#endif

// ---- Analog sensing pins -----------------------------------
#ifndef ROVER_VBAT_ADC_PIN
#define ROVER_VBAT_ADC_PIN A0
#endif
#ifndef ROVER_TEMP_ADC_PIN
#define ROVER_TEMP_ADC_PIN A3
#endif

// ---- BTS7960 current-sense IS pins -------------------------
#ifndef ROVER_CURR_L_ADC_PIN
#define ROVER_CURR_L_ADC_PIN A4
#endif
#ifndef ROVER_CURR_R_ADC_PIN
#define ROVER_CURR_R_ADC_PIN A5
#endif

// ---- Calibration -------------------------------------------
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

// ---- Drive behavior tuning ----------------------------------
#ifndef ROVER_MAX_PWM
#define ROVER_MAX_PWM 255
#endif
#ifndef ROVER_MIN_PWM
#define ROVER_MIN_PWM 0
#endif
#ifndef ROVER_RAMP_SEC
#define ROVER_RAMP_SEC 1.0f
#endif

// ---- Safety -------------------------------------------------
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

constexpr uint8_t VBAT_ADC_PIN   = ROVER_VBAT_ADC_PIN;
constexpr uint8_t CURR_L_ADC_PIN = ROVER_CURR_L_ADC_PIN;
constexpr uint8_t CURR_R_ADC_PIN = ROVER_CURR_R_ADC_PIN;
constexpr uint8_t TEMP_ADC_PIN   = ROVER_TEMP_ADC_PIN;

constexpr float VBAT_DIV_RATIO      = ROVER_VBAT_DIV_RATIO;
constexpr float CURR_ZERO_MV        = ROVER_CURR_ZERO_MV;
constexpr float CURR_SENS_MV_PER_A  = ROVER_CURR_SENS_MV_PER_A;

constexpr uint32_t WATCHDOG_MS       = ROVER_WATCHDOG_MS;
constexpr uint32_t TELEM_INTERVAL_MS = ROVER_TELEM_INTERVAL_MS;

constexpr uint8_t MAX_PWM       = ROVER_MAX_PWM;
constexpr uint8_t MIN_PWM       = ROVER_MIN_PWM;
constexpr float   RAMP_SEC      = ROVER_RAMP_SEC;

constexpr float LOW_VOLTAGE_CUTOFF = ROVER_LOW_VOLTAGE_CUTOFF;
constexpr float LOW_VOLTAGE_RESUME = ROVER_LOW_VOLTAGE_RESUME;
constexpr float INPUT_DEADBAND     = ROVER_INPUT_DEADBAND;
constexpr bool  REQUIRE_ARM        = ROVER_REQUIRE_ARM;

} // namespace fwcfg
