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

struct MotorTuning {
    float maxForward = 1.0f;     // 0..1
    float maxReverse = 1.0f;     // 0..1
    float maxTurn    = 1.0f;     // 0..1
    float throttleExpo = 1.0f;   // >= 0.2 (1.0 = linear)
    float turnExpo     = 1.0f;   // >= 0.2 (1.0 = linear)
    float accelUpPerSec   = 3.0f; // normalized speed/s
    float accelDownPerSec = 5.0f; // normalized speed/s
};

struct MotorPins {
    uint8_t rpwm;   // forward PWM pin
    uint8_t lpwm;   // reverse PWM pin
    uint8_t en;     // enable pin (active HIGH)
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
        // Start fully disabled, then stop() sets known-safe output states.
        digitalWrite(left_.en,  LOW);
        digitalWrite(right_.en, LOW);
        stop();
    }

    // speed: -1.0 (full reverse) to +1.0 (full forward)
    void setLeft(float speed)  { setMotor(left_,  speed); }
    void setRight(float speed) { setMotor(right_, speed); }

    void drive(float leftSpeed, float rightSpeed) {
        const uint32_t now = millis();
        float dt = (lastUpdateMs_ == 0) ? 0.033f : (float)(now - lastUpdateMs_) / 1000.0f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.25f) dt = 0.25f;
        lastUpdateMs_ = now;

        // Reconstruct throttle/turn so we can cap and shape each axis independently.
        float throttle = (leftSpeed + rightSpeed) * 0.5f;
        // Turn axis ignored — car chassis, both motors drive same direction.
        // Turn will be handled by a separate steering motor later.

        throttle = constrain(throttle, -1.0f, 1.0f);

        if (throttle >= 0.0f) {
            throttle = min(throttle, constrain(tuning_.maxForward, 0.0f, 1.0f));
        } else {
            throttle = max(throttle, -constrain(tuning_.maxReverse, 0.0f, 1.0f));
        }

        throttle = applyExpo(throttle, max(0.2f, tuning_.throttleExpo));

        const float targetL = constrain(throttle, -1.0f, 1.0f);
        const float targetR = constrain(throttle, -1.0f, 1.0f);

        outLeft_ = slew(outLeft_, targetL, dt);
        outRight_ = slew(outRight_, targetR, dt);

        setLeft(outLeft_);
        setRight(outRight_);
    }

    void stop() {
        outLeft_ = 0.0f;
        outRight_ = 0.0f;
        setLeft(0.0f);
        setRight(0.0f);
        lastUpdateMs_ = millis();
    }

    void enable(bool en) {
        digitalWrite(left_.en,  en ? HIGH : LOW);
        digitalWrite(right_.en, en ? HIGH : LOW);
    }

private:
    static float applyExpo(float v, float expo) {
        const float sign = (v < 0.0f) ? -1.0f : 1.0f;
        return sign * powf(fabsf(v), expo);
    }

    // Force slew through zero when direction reverses (prevents current spike)
    float slew(float current, float target, float dt) const {
        const float delta = target - current;
        // If crossing zero (direction reversal), force decel to zero first
        if (current > MOTOR_ZERO_EPSILON && target < -MOTOR_ZERO_EPSILON) {
            const float rate = max(0.05f, tuning_.accelDownPerSec);
            const float maxStep = rate * dt;
            float next = current - maxStep;
            return (next <= 0.0f) ? 0.0f : next;
        }
        if (current < -MOTOR_ZERO_EPSILON && target > MOTOR_ZERO_EPSILON) {
            const float rate = max(0.05f, tuning_.accelDownPerSec);
            const float maxStep = rate * dt;
            float next = current + maxStep;
            return (next >= 0.0f) ? 0.0f : next;
        }
        const float curMag = fabsf(current);
        const float tgtMag = fabsf(target);
        const bool accelerating = tgtMag > curMag;
        const float rate = accelerating ? max(0.05f, tuning_.accelUpPerSec)
                                        : max(0.05f, tuning_.accelDownPerSec);
        const float maxStep = rate * dt;
        if (fabsf(delta) <= maxStep) return target;
        return current + ((delta > 0.0f) ? maxStep : -maxStep);
    }

    void setMotor(const MotorPins& m, float speed) {
        speed = constrain(speed, -1.f, 1.f);
        if (fabsf(speed) <= MOTOR_ZERO_EPSILON) {
            // Brake: both PWM low, keep EN high briefly to let current decay
            analogWrite(m.rpwm, 0);
            analogWrite(m.lpwm, 0);
            digitalWrite(m.en, HIGH);
            return;
        }

        digitalWrite(m.en, HIGH);
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
    MotorTuning tuning_;
    float outLeft_ = 0.0f;
    float outRight_ = 0.0f;
    uint32_t lastUpdateMs_ = 0;
};
