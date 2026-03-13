#pragma once
// ============================================================
//  MotorController — BTS7960 dual H-bridge, car chassis
//  nRF52840 variant — uses analogWrite (PWM) same as Teensy.
//  Adafruit nRF52 Arduino core provides analogWrite on all pins.
// ============================================================
#include <Arduino.h>

struct MotorTuning {
    uint8_t maxPwm     = 255;
    uint8_t minPwm     = 0;
    float   rampSec    = 1.0f;
    bool    invertLeft  = false;
    bool    invertRight = false;
};

struct MotorPins {
    uint8_t rpwm;
    uint8_t lpwm;
    uint8_t en;
};

static constexpr float MOTOR_ZERO_EPSILON = 0.01f;

class MotorController {
public:
    MotorController(const MotorPins& left, const MotorPins& right, const MotorTuning& tuning = MotorTuning())
        : left_(left), right_(right), tuning_(tuning) {}

    void begin() {
        pinMode(left_.rpwm, OUTPUT);
        pinMode(left_.lpwm, OUTPUT);
        pinMode(left_.en,   OUTPUT);
        pinMode(right_.rpwm, OUTPUT);
        pinMode(right_.lpwm, OUTPUT);
        pinMode(right_.en,   OUTPUT);
        digitalWrite(left_.en,  LOW);
        digitalWrite(right_.en, LOW);
        stop();
    }

    void setTarget(float leftIn, float rightIn) {
        float throttle = constrain((leftIn + rightIn) * 0.5f, -1.0f, 1.0f);
        target_ = mapThrottle(throttle);
    }

    void tick() {
        const uint32_t now = millis();
        float dt = (lastUpdateMs_ == 0) ? 0.001f : (float)(now - lastUpdateMs_) / 1000.0f;
        if (dt < 0.001f) return;
        if (dt > 0.25f) dt = 0.25f;
        lastUpdateMs_ = now;

        output_ = slew(output_, target_, dt);

        setMotor(left_,  tuning_.invertLeft  ? -output_ : output_);
        setMotor(right_, tuning_.invertRight ? -output_ : output_);
    }

    void coast() { target_ = 0.0f; }

    void stop() {
        target_ = 0.0f;
        output_ = 0.0f;
        setMotor(left_, 0.0f);
        setMotor(right_, 0.0f);
        lastUpdateMs_ = millis();
    }

    void enable(bool en) {
        digitalWrite(left_.en,  en ? HIGH : LOW);
        digitalWrite(right_.en, en ? HIGH : LOW);
    }

    float output() const { return output_; }

private:
    float mapThrottle(float input) const {
        if (fabsf(input) <= MOTOR_ZERO_EPSILON) return 0.0f;
        const float sign = (input < 0.0f) ? -1.0f : 1.0f;
        const float mag = fabsf(input);
        const float minF = tuning_.minPwm / 255.0f;
        const float maxF = tuning_.maxPwm / 255.0f;
        if (maxF <= 0.0f) return 0.0f;
        const float mapped = minF + mag * (maxF - minF);
        return sign * constrain(mapped, 0.0f, 1.0f);
    }

    float slew(float current, float target, float dt) const {
        if (current > MOTOR_ZERO_EPSILON && target < -MOTOR_ZERO_EPSILON) {
            float next = current - rampRate() * dt;
            return (next <= 0.0f) ? 0.0f : next;
        }
        if (current < -MOTOR_ZERO_EPSILON && target > MOTOR_ZERO_EPSILON) {
            float next = current + rampRate() * dt;
            return (next >= 0.0f) ? 0.0f : next;
        }
        const float delta = target - current;
        const float maxStep = rampRate() * dt;
        if (fabsf(delta) <= maxStep) return target;
        return current + ((delta > 0.0f) ? maxStep : -maxStep);
    }

    float rampRate() const { return 1.0f / max(0.05f, tuning_.rampSec); }

    void setMotor(const MotorPins& m, float speed) {
        speed = constrain(speed, -1.f, 1.f);
        if (fabsf(speed) <= MOTOR_ZERO_EPSILON) {
            analogWrite(m.rpwm, 0);
            analogWrite(m.lpwm, 0);
            digitalWrite(m.en, HIGH);  // brake mode
        } else if (speed > 0.f) {
            int pwm = (int)(speed * 255.f);
            analogWrite(m.rpwm, pwm);
            analogWrite(m.lpwm, 0);
            digitalWrite(m.en, HIGH);
        } else {
            int pwm = (int)(-speed * 255.f);
            analogWrite(m.rpwm, 0);
            analogWrite(m.lpwm, pwm);
            digitalWrite(m.en, HIGH);
        }
    }

    MotorPins left_, right_;
    MotorTuning tuning_;
    float target_ = 0.0f;
    float output_ = 0.0f;
    uint32_t lastUpdateMs_ = 0;
};
