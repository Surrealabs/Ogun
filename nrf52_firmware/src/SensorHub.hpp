#pragma once
// ============================================================
//  SensorHub — nRF52840 variant
//
//  Dual current sense via ADC.  Encoders use interrupt-based
//  counting (no Teensy Encoder library on nRF52).
//  nRF52840 ADC: 12-bit (0-4095), default 3.6 V reference.
// ============================================================
#include <Arduino.h>

struct SensorConfig {
    uint8_t vbatAdcPin;
    uint8_t currLAdcPin;
    uint8_t currRAdcPin;
    uint8_t tempAdcPin;
    float vbatDivRatio;
    float currZeroMv;
    float currSensMvPerA;
};

// Interrupt-driven quadrature encoder (simple 1x counting)
struct EncoderState {
    volatile long ticks;
    uint8_t pinA;
    uint8_t pinB;
};

static EncoderState encStates_[2];

static void encISR0() {
    encStates_[0].ticks += digitalRead(encStates_[0].pinB) ? 1 : -1;
}
static void encISR1() {
    encStates_[1].ticks += digitalRead(encStates_[1].pinB) ? 1 : -1;
}

class SensorHub {
public:
    SensorHub(uint8_t encLA, uint8_t encLB,
              uint8_t encRA, uint8_t encRB,
              const SensorConfig& cfg)
        : cfg_(cfg) {
        encStates_[0] = {0, encLA, encLB};
        encStates_[1] = {0, encRA, encRB};
    }

    void begin() {
        analogReadResolution(12);

        // Set up encoder interrupts
        pinMode(encStates_[0].pinA, INPUT_PULLUP);
        pinMode(encStates_[0].pinB, INPUT_PULLUP);
        pinMode(encStates_[1].pinA, INPUT_PULLUP);
        pinMode(encStates_[1].pinB, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(encStates_[0].pinA), encISR0, RISING);
        attachInterrupt(digitalPinToInterrupt(encStates_[1].pinA), encISR1, RISING);
    }

    void update() {
        encLTicks_ = encStates_[0].ticks;
        encRTicks_ = encStates_[1].ticks;
        voltage_   = 0.f;
        temp_      = 0.f;

        // nRF52840 ADC: 12-bit, 3.6V reference
        float rawL = (float)analogRead(cfg_.currLAdcPin) / 4095.f * 3600.f; // mV
        currentL_  = (rawL - cfg_.currZeroMv) / cfg_.currSensMvPerA;

        float rawR = (float)analogRead(cfg_.currRAdcPin) / 4095.f * 3600.f; // mV
        currentR_  = (rawR - cfg_.currZeroMv) / cfg_.currSensMvPerA;
    }

    void toJson(char* buf, size_t bufLen) {
        snprintf(buf, bufLen,
            "{\"type\":\"sensors\","
            "\"enc_l\":%ld,\"enc_r\":%ld,"
            "\"volt\":%.2f,\"curr_l\":%.2f,\"curr_r\":%.2f,\"temp\":%.1f}",
            (long)encLTicks_, (long)encRTicks_,
            voltage_, currentL_, currentR_, temp_);
    }

    void resetEncoders() {
        encStates_[0].ticks = 0;
        encStates_[1].ticks = 0;
    }

    long  encL()  const { return encLTicks_; }
    long  encR()  const { return encRTicks_; }
    float volt()  const { return voltage_; }
    float currL() const { return currentL_; }
    float currR() const { return currentR_; }
    float temp()  const { return temp_;    }

private:
    SensorConfig cfg_;
    long    encLTicks_{0}, encRTicks_{0};
    float   voltage_{0.f}, currentL_{0.f}, currentR_{0.f}, temp_{0.f};
};
