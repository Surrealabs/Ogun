#pragma once
// ============================================================
//  SensorHub — dual current sense + stub telemetry for bring-up.
//  Voltage, temp, encoders disabled (no hardware wired).
//  Two BTS7960 IS pins read for left/right motor current.
// ============================================================
#include <Arduino.h>
#include <Encoder.h>

struct SensorConfig {
    uint8_t vbatAdcPin;
    uint8_t currLAdcPin;   // left BTS7960 IS pin
    uint8_t currRAdcPin;   // right BTS7960 IS pin
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
        analogReadResolution(12);   // Teensy 4.x 12-bit ADC
        analogReadAveraging(16);
    }

    void update() {
        encLTicks_ = 0;
        encRTicks_ = 0;
        voltage_   = 0.f;
        temp_      = 0.f;

        // Left motor current sense (BTS7960 IS → pin 19/A5)
        float rawL  = (float)analogRead(cfg_.currLAdcPin) / 4095.f * 3300.f; // mV
        currentL_   = (rawL - cfg_.currZeroMv) / cfg_.currSensMvPerA;

        // Right motor current sense (BTS7960 IS → pin 16/A2)
        float rawR  = (float)analogRead(cfg_.currRAdcPin) / 4095.f * 3300.f; // mV
        currentR_   = (rawR - cfg_.currZeroMv) / cfg_.currSensMvPerA;
    }

    void toJson(char* buf, size_t bufLen) {
        snprintf(buf, bufLen,
            "{\"type\":\"sensors\","
            "\"enc_l\":%ld,\"enc_r\":%ld,"
            "\"volt\":%.2f,\"curr_l\":%.2f,\"curr_r\":%.2f,\"temp\":%.1f}",
            (long)encLTicks_, (long)encRTicks_,
            voltage_, currentL_, currentR_, temp_);
    }

    void resetEncoders() { encL_.write(0); encR_.write(0); }

    long  encL()  const { return encLTicks_; }
    long  encR()  const { return encRTicks_; }
    float volt()  const { return voltage_; }
    float currL() const { return currentL_; }
    float currR() const { return currentR_; }
    float temp()  const { return temp_;    }

private:
    Encoder encL_, encR_;
    SensorConfig cfg_;
    long    encLTicks_{0}, encRTicks_{0};
    float   voltage_{0.f}, currentL_{0.f}, currentR_{0.f}, temp_{0.f};
};
