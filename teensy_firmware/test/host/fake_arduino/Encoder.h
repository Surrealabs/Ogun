#pragma once
// Fake Encoder library for firmware_sim_test (SensorHub reads no encoders today).
#include <stdint.h>

class Encoder {
public:
    Encoder(uint8_t, uint8_t) {}
    long read() { return 0; }
    void write(long) {}
};
