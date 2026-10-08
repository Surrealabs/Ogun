#pragma once
// ============================================================
//  Fake Arduino.h for host-compiling the REAL teensy_firmware
//  sources (main.cpp, MotorController.hpp, SensorHub.hpp) in
//  firmware_sim_test.cpp. Only what those files use. Pin writes
//  are recorded, millis() is a settable clock, Serial is a byte
//  queue in and a line list out. Not a Teensy model: no timers,
//  no interrupts, no electrical behaviour.
// ============================================================
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>
#include <string>
#include <vector>

#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0

// Teensy 4.1 analog pin numbers used by FirmwareConfig.hpp defaults.
#define A0 14
#define A1 15
#define A3 17
#define A7 21

namespace fake {
constexpr int kPins = 64;
inline uint32_t g_millis = 0;
inline bool     g_autoTick = false;      // advance 1 ms on every millis() call
inline int      g_mode[kPins] = {};
inline int      g_level[kPins] = {};     // last digitalWrite
inline int      g_pwm[kPins] = {};       // last analogWrite
inline int      g_digitalWrites[kPins] = {};
inline bool     g_rebooted = false;
inline void reset() {
    for (int i = 0; i < kPins; ++i) { g_mode[i] = -1; g_level[i] = 0; g_pwm[i] = 0; g_digitalWrites[i] = 0; }
    g_rebooted = false;
}
} // namespace fake

inline uint32_t millis() {
    const uint32_t t = fake::g_millis;
    if (fake::g_autoTick) ++fake::g_millis;
    return t;
}
inline void delay(uint32_t ms) { fake::g_millis += ms; }
inline void pinMode(uint8_t pin, int mode) { if (pin < fake::kPins) fake::g_mode[pin] = mode; }
inline void digitalWrite(uint8_t pin, int v) {
    if (pin < fake::kPins) { fake::g_level[pin] = v ? HIGH : LOW; ++fake::g_digitalWrites[pin]; }
}
inline int  digitalRead(uint8_t pin) { return pin < fake::kPins ? fake::g_level[pin] : 0; }
inline void analogWrite(uint8_t pin, int v) { if (pin < fake::kPins) fake::g_pwm[pin] = v; }
inline int  analogRead(uint8_t) { return 0; }
inline void analogReadResolution(int) {}
inline void analogReadAveraging(int) {}

template <class T, class L, class H>
inline T constrain(T amt, L low, H high) {      // same comparisons as Teensy's macro
    return (amt < low) ? (T)low : ((amt > high) ? (T)high : amt);
}
template <class A, class B>
inline A max(A a, B b) { return (a > b) ? a : (A)b; }

class String {
public:
    String(const char* s) : s_(s) {}
    String(const std::string& s) : s_(s) {}
    String(float v, int digits) { char b[48]; snprintf(b, sizeof(b), "%.*f", digits, (double)v); s_ = b; }
    const char* c_str() const { return s_.c_str(); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s_); }
    friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
private:
    std::string s_;
};

class FakeSerial {
public:
    std::deque<char> in;
    std::vector<std::string> out;
    void begin(unsigned long) {}
    explicit operator bool() const { return true; }
    int available() const { return (int)in.size(); }
    int read() {
        if (in.empty()) return -1;
        const char c = in.front();
        in.pop_front();
        return (unsigned char)c;
    }
    void println(const char* s) { out.emplace_back(s); }
    void println(const String& s) { out.emplace_back(s.c_str()); }
    void flush() {}
    void feed(const std::string& bytes) { for (char c : bytes) in.push_back(c); }
};
inline FakeSerial Serial;
