#pragma once
// ============================================================
//  MotorController — BTS7960 dual H-bridge driver (tank drive)
//
//  BTS7960 wiring per channel:
//    RPWM → forward PWM   (analogWrite 0-255)
//    LPWM → reverse PWM   (analogWrite 0-255)
//    R_EN / L_EN → enable (HIGH to enable)
//    VCC  → 5 V logic
//    B+   → battery positive
//    B-   → battery GND
//
//  Two BTS7960 boards = one full H-bridge per motor (left & right).
// ============================================================
#include <Arduino.h>

struct MotorPins {
    uint8_t rpwm;   // forward PWM pin
    uint8_t lpwm;   // reverse PWM pin
    uint8_t en;     // enable pin (active HIGH)
};

class MotorController {
public:
    MotorController(const MotorPins& left, const MotorPins& right)
        : left_(left), right_(right) {}

    void begin() {
        pinMode(left_.rpwm, OUTPUT);
        pinMode(left_.lpwm, OUTPUT);
        pinMode(left_.en,   OUTPUT);
        pinMode(right_.rpwm, OUTPUT);
        pinMode(right_.lpwm, OUTPUT);
        pinMode(right_.en,   OUTPUT);
        digitalWrite(left_.en,  HIGH);
        digitalWrite(right_.en, HIGH);
        stop();
    }

    // speed: -1.0 (full reverse) to +1.0 (full forward)
    void setLeft(float speed)  { setMotor(left_,  speed); }
    void setRight(float speed) { setMotor(right_, speed); }

    void drive(float leftSpeed, float rightSpeed) {
        setLeft(leftSpeed);
        setRight(rightSpeed);
    }

    void stop() {
        drive(0.f, 0.f);
    }

    void enable(bool en) {
        digitalWrite(left_.en,  en ? HIGH : LOW);
        digitalWrite(right_.en, en ? HIGH : LOW);
    }

private:
    void setMotor(const MotorPins& m, float speed) {
        speed = constrain(speed, -1.f, 1.f);
        uint8_t pwm = (uint8_t)(fabsf(speed) * 255.f);
        if (speed >= 0.f) {
            analogWrite(m.rpwm, pwm);
            analogWrite(m.lpwm, 0);
        } else {
            analogWrite(m.rpwm, 0);
            analogWrite(m.lpwm, pwm);
        }
    }

    MotorPins left_, right_;
};
