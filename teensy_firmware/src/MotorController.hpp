#pragma once
// ============================================================
//  MotorController — BTS7960 dual H-bridge, car chassis
//
//  Simple tuning model:
//    maxPwm     — PWM ceiling (0-255)
//    minPwm     — PWM floor when moving (overcomes static friction)
//    rampSec    — seconds to ramp from 0 → full power
//    invertLeft / invertRight — flip motor direction
//
//  Both motors always receive the same throttle (car chassis).
// ============================================================
#include <Arduino.h>

struct MotorTuning {
    uint8_t maxPwm     = 255;    // PWM ceiling (0-255)
    uint8_t minPwm     = 0;      // PWM floor when moving (0-255)
    float   rampSec    = 1.0f;   // seconds to ramp 0 → full
    bool    invertLeft  = false;
    bool    invertRight = false;
};

struct MotorPins {
    uint8_t rpwm;   // forward PWM pin
    uint8_t lpwm;   // reverse PWM pin
    uint8_t en;     // enable pin (active HIGH)
};

static constexpr float MOTOR_ZERO_EPSILON = 0.01f;
static constexpr float DECEL_SECS = 0.15f;  // fixed fast decel

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

    void setLeft(float speed)  { setMotor(left_,  tuning_.invertLeft  ? -speed : speed); }
    void setRight(float speed) { setMotor(right_, tuning_.invertRight ? -speed : speed); }

    // Both l and r should be identical (car chassis), range -1..1
    void drive(float leftIn, float rightIn) {
        const uint32_t now = millis();
        float dt = (lastUpdateMs_ == 0) ? 0.033f : (float)(now - lastUpdateMs_) / 1000.0f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.25f) dt = 0.25f;
        lastUpdateMs_ = now;

        // Car chassis: average to single throttle, ignore turn
        float throttle = constrain((leftIn + rightIn) * 0.5f, -1.0f, 1.0f);

        // Map joystick → PWM-normalized target with min/max
        float target = mapThrottle(throttle);

        // Slew-rate limit (accel uses rampSec, decel is always fast)
        output_ = slew(output_, target, dt);

        setLeft(output_);
        setRight(output_);
    }

    void stop() {
        output_ = 0.0f;
        setLeft(0.0f);
        setRight(0.0f);
        lastUpdateMs_ = millis();
    }

    void enable(bool en) {
        digitalWrite(left_.en,  en ? HIGH : LOW);
        digitalWrite(right_.en, en ? HIGH : LOW);
    }

private:
    // Map joystick -1..1 → normalized output incorporating min/max PWM
    float mapThrottle(float input) const {
        if (fabsf(input) <= MOTOR_ZERO_EPSILON) return 0.0f;
        const float sign = (input < 0.0f) ? -1.0f : 1.0f;
        const float mag = fabsf(input);
        const float minF = tuning_.minPwm / 255.0f;
        const float maxF = tuning_.maxPwm / 255.0f;
        if (maxF <= 0.0f) return 0.0f;
        // Linear: tiny input → minF, full input → maxF
        const float mapped = minF + mag * (maxF - minF);
        return sign * constrain(mapped, 0.0f, 1.0f);
    }

    // Force decel through zero on direction reversal
    float slew(float current, float target, float dt) const {
        if (current > MOTOR_ZERO_EPSILON && target < -MOTOR_ZERO_EPSILON) {
            float next = current - decelRate() * dt;
            return (next <= 0.0f) ? 0.0f : next;
        }
        if (current < -MOTOR_ZERO_EPSILON && target > MOTOR_ZERO_EPSILON) {
            float next = current + decelRate() * dt;
            return (next >= 0.0f) ? 0.0f : next;
        }
        const float delta = target - current;
        const bool accelerating = fabsf(target) > fabsf(current);
        const float rate = accelerating ? accelRate() : decelRate();
        const float maxStep = rate * dt;
        if (fabsf(delta) <= maxStep) return target;
        return current + ((delta > 0.0f) ? maxStep : -maxStep);
    }

    float accelRate() const { return 1.0f / max(0.05f, tuning_.rampSec); }
    float decelRate() const { return 1.0f / DECEL_SECS; }

    void setMotor(const MotorPins& m, float speed) {
        speed = constrain(speed, -1.f, 1.f);
        if (fabsf(speed) <= MOTOR_ZERO_EPSILON) {
            analogWrite(m.rpwm, 0);
            analogWrite(m.lpwm, 0);
            digitalWrite(m.en, HIGH);  // brake mode
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
    float output_ = 0.0f;
    uint32_t lastUpdateMs_ = 0;
};
