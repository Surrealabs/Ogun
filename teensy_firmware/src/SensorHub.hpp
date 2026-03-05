#pragma once
// ============================================================
//  SensorHub — aggregates encoder ticks, voltage, current,
//  and temperature, and serialises them to JSON for the Pi
// ============================================================
#include <Arduino.h>
#include <Encoder.h>

// ---- Voltage divider / current sense pin config ------------
// Adjust VBAT_DIV_RATIO to match your voltage divider:
//   e.g. 10 kΩ + 3.3 kΩ → ratio = (10+3.3)/3.3 ≈ 4.03
//   Measure raw ADC value with a known voltage and calibrate.
#define VBAT_ADC_PIN      A0
#define VBAT_DIV_RATIO    4.03f

// ACS712 / hall-effect current sensor on A1
// 30A module: 66 mV/A, zero at VCC/2 (~512 on 10-bit ADC)
#define CURR_ADC_PIN      A1
#define CURR_SENS_MV_PER_A 66.0f

// NTC thermistor on A2 (optional — leave disconnected if unused)
#define TEMP_ADC_PIN      A2

class SensorHub {
public:
    SensorHub(uint8_t encLA, uint8_t encLB,
              uint8_t encRA, uint8_t encRB)
        : encL_(encLA, encLB), encR_(encRA, encRB) {}

    void begin() {
        analogReadResolution(12);   // Teensy 4.x supports 12-bit
        analogReadAveraging(16);
    }

    // Call periodically (e.g. every 100 ms) to update readings
    void update() {
        encLTicks_ = encL_.read();
        encRTicks_ = encR_.read();

        // Battery voltage
        float rawV   = (float)analogRead(VBAT_ADC_PIN) / 4095.f * 3.3f;
        voltage_     = rawV * VBAT_DIV_RATIO;

        // Current sense (bidirectional)
        float rawC   = (float)analogRead(CURR_ADC_PIN) / 4095.f * 3300.f; // mV
        current_     = (rawC - 1650.f) / CURR_SENS_MV_PER_A;

        // Temperature (NTC Steinhart-Hart simplified)
        float rawT   = (float)analogRead(TEMP_ADC_PIN);
        if (rawT > 10.f) {
            float R     = 10000.f * (4095.f / rawT - 1.f); // 10 kΩ pull-up
            float lnR   = logf(R / 10000.f);
            float T_inv = (1.f/298.15f) + lnR / 3950.f;    // B=3950
            temp_       = (1.f / T_inv) - 273.15f;
        }
    }

    // Compose telemetry JSON (no heap allocation)
    void toJson(char* buf, size_t bufLen) {
        snprintf(buf, bufLen,
            "{\"type\":\"sensors\","
            "\"enc_l\":%ld,\"enc_r\":%ld,"
            "\"volt\":%.2f,\"curr\":%.2f,\"temp\":%.1f}",
            (long)encLTicks_, (long)encRTicks_,
            voltage_, current_, temp_);
    }

    void resetEncoders() { encL_.write(0); encR_.write(0); }

    long  encL() const { return encLTicks_; }
    long  encR() const { return encRTicks_; }
    float volt()  const { return voltage_; }
    float curr()  const { return current_; }
    float temp()  const { return temp_;    }

private:
    Encoder encL_, encR_;
    long    encLTicks_{0}, encRTicks_{0};
    float   voltage_{0.f}, current_{0.f}, temp_{25.f};
};
