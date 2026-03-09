#pragma once
// ============================================================
//  SensorHub — bare-bones telemetry for current sensing.
//  Encoder/voltage/temperature are intentionally disabled for
//  minimal bring-up and report as 0 in telemetry JSON.
// ============================================================
#include <Arduino.h>
#include <Encoder.h>

struct SensorConfig {
    uint8_t vbatAdcPin;
    uint8_t currAdcPin;
    uint8_t tempAdcPin;
    float vbatDivRatio;
    float currZeroMv;
    float currSensMvPerA;
};

class SensorHub {
public:
    SensorHub(uint8_t encLA, uint8_t encLB,
          uint8_t encRA, uint8_t encRB,
          const SensorConfig& cfg)
      : encL_(encLA, encLB), encR_(encRA, encRB), cfg_(cfg) {}

    void begin() {
        analogReadResolution(12);   // Teensy 4.x supports 12-bit
        analogReadAveraging(16);
    }

    // Call periodically (e.g. every 100 ms) to update readings
    void update() {
        // Bare-bones mode: disable encoder and VBAT/temperature sensing
        // until corresponding hardware is wired and validated.
        encLTicks_ = 0;
        encRTicks_ = 0;
        voltage_   = 0.f;

        // Current sense (bidirectional)
        float rawC   = (float)analogRead(cfg_.currAdcPin) / 4095.f * 3300.f; // mV
        current_     = (rawC - cfg_.currZeroMv) / cfg_.currSensMvPerA;

        temp_ = 0.f;
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
    SensorConfig cfg_;
    long    encLTicks_{0}, encRTicks_{0};
    float   voltage_{0.f}, current_{0.f}, temp_{25.f};
};
