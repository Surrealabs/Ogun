#pragma once
// ============================================================
//  MotorController — BTS7960 dual H-bridge + turning motor
//
//  Simple tuning model:
//    maxPwm     — PWM ceiling (0-255)
//    minPwm     — PWM floor when moving (overcomes static friction)
//    rampSec    — seconds to ramp from 0 → full power
//    invertLeft / invertRight — flip motor direction
//
//  Left/right motors always receive the same throttle (car chassis).
//  Turning motor is independent (steering actuator).
//  Call tick() every loop iteration to keep the slew running.
// ============================================================
#include <Arduino.h>

struct MotorTuning {
    uint8_t maxPwm     = 255;    // PWM ceiling (0-255) for drive motors
    uint8_t minPwm     = 0;      // PWM floor when moving (0-255)
    float   rampSec    = 1.0f;   // seconds to ramp 0 → full
    bool    invertLeft  = false;
    bool    invertRight = false;
    uint8_t turnMaxPwm = 255;    // PWM ceiling for turn motor
    bool    invertTurn  = false;
    float   turnSlowdown = 0.5f; // 0-1: fraction to reduce drive at full turn
    float   turnRampSec = 0.1f;  // seconds to ramp turn motor (fast by default)
};

struct MotorPins {
    uint8_t rpwm;   // forward PWM pin
    uint8_t lpwm;   // reverse PWM pin
    uint8_t en;     // enable pin (active HIGH)
};

static constexpr float MOTOR_ZERO_EPSILON = 0.01f;

class MotorController {
public:
    MotorController(const MotorPins& left, const MotorPins& right,
                    const MotorPins& turn, const MotorTuning& tuning = MotorTuning())
        : left_(left), right_(right), turn_(turn), tuning_(tuning) {}

    void begin() {
        pinMode(left_.rpwm, OUTPUT);
        pinMode(left_.lpwm, OUTPUT);
        pinMode(left_.en,   OUTPUT);
        pinMode(right_.rpwm, OUTPUT);
        pinMode(right_.lpwm, OUTPUT);
        pinMode(right_.en,   OUTPUT);
        pinMode(turn_.rpwm, OUTPUT);
        pinMode(turn_.lpwm, OUTPUT);
        pinMode(turn_.en,   OUTPUT);
        digitalWrite(left_.en,  LOW);
        digitalWrite(right_.en, LOW);
        digitalWrite(turn_.en,  LOW);
        stop();
    }

    // Set a new drive target. Call this when a drive command arrives.
    // Both l and r should be identical (car chassis), range -1..1
    void setTarget(float leftIn, float rightIn) {
        // Car chassis: average to single throttle, ignore turn
        float throttle = constrain((leftIn + rightIn) * 0.5f, -1.0f, 1.0f);
        target_ = mapThrottle(throttle, tuning_.maxPwm);
    }

    // Set a new turn target for the steering motor.
    // Range -1..1 (left..right). Independent of drive throttle.
    void setTurnTarget(float turnIn) {
        turnIn = constrain(turnIn, -1.0f, 1.0f);
        turnTarget_ = mapThrottle(turnIn, tuning_.turnMaxPwm);
    }

    // Call every loop iteration. Advances the slew ramp by dt and
    // writes PWM to the motors. This keeps the ramp smooth even
    // when drive commands arrive at a lower rate than loop().
    void tick() {
        const uint32_t now = millis();
        float dt = (lastUpdateMs_ == 0) ? 0.001f : (float)(now - lastUpdateMs_) / 1000.0f;
        if (dt < 0.001f) return;  // too soon, skip
        if (dt > 0.25f) dt = 0.25f;
        lastUpdateMs_ = now;

        output_ = slew(output_, target_, dt, rampRate());
        turnOutput_ = slew(turnOutput_, turnTarget_, dt, turnRampRate());

        // Reduce drive power proportionally to turn magnitude
        float turnMag = fabsf(turnOutput_);
        float driveScale = 1.0f - tuning_.turnSlowdown * turnMag;
        float scaledOut = output_ * driveScale;

        setMotor(left_,  tuning_.invertLeft  ? -scaledOut : scaledOut);
        setMotor(right_, tuning_.invertRight ? -scaledOut : scaledOut);
        setMotor(turn_,  tuning_.invertTurn  ? -turnOutput_ : turnOutput_);
    }

    // Coast to zero: set target to 0, let slew handle it gently.
    void coast() { target_ = 0.0f; turnTarget_ = 0.0f; }

    // Hard stop: zero everything immediately. Only for e-stop/disarm.
    void stop() {
        target_ = 0.0f;
        output_ = 0.0f;
        turnTarget_ = 0.0f;
        turnOutput_ = 0.0f;
        setMotor(left_, 0.0f);
        setMotor(right_, 0.0f);
        setMotor(turn_, 0.0f);
        lastUpdateMs_ = millis();
    }

    void enable(bool en) {
        digitalWrite(left_.en,  en ? HIGH : LOW);
        digitalWrite(right_.en, en ? HIGH : LOW);
        digitalWrite(turn_.en,  en ? HIGH : LOW);
    }

    float output() const { return output_; }
    float turnOutput() const { return turnOutput_; }

private:
    // Map joystick -1..1 → normalized output incorporating min/max PWM
    float mapThrottle(float input, uint8_t ceiling) const {
        if (fabsf(input) <= MOTOR_ZERO_EPSILON) return 0.0f;
        const float sign = (input < 0.0f) ? -1.0f : 1.0f;
        const float mag = fabsf(input);
        const float minF = tuning_.minPwm / 255.0f;
        const float maxF = ceiling / 255.0f;
        if (maxF <= 0.0f) return 0.0f;
        const float mapped = minF + mag * (maxF - minF);
        return sign * constrain(mapped, 0.0f, 1.0f);
    }

    // Slew with direction-reversal protection
    float slew(float current, float target, float dt, float rate) const {
        // Force decel through zero on direction reversal
        if (current > MOTOR_ZERO_EPSILON && target < -MOTOR_ZERO_EPSILON) {
            float next = current - rate * dt;
            return (next <= 0.0f) ? 0.0f : next;
        }
        if (current < -MOTOR_ZERO_EPSILON && target > MOTOR_ZERO_EPSILON) {
            float next = current + rate * dt;
            return (next >= 0.0f) ? 0.0f : next;
        }
        const float delta = target - current;
        const float maxStep = rate * dt;
        if (fabsf(delta) <= maxStep) return target;
        return current + ((delta > 0.0f) ? maxStep : -maxStep);
    }

    float rampRate() const { return 1.0f / max(0.05f, tuning_.rampSec); }
    float turnRampRate() const { return 1.0f / max(0.05f, tuning_.turnRampSec); }

    void setMotor(const MotorPins& m, float speed) {
        speed = constrain(speed, -1.f, 1.f);
        if (fabsf(speed) <= MOTOR_ZERO_EPSILON) {
            analogWrite(m.rpwm, 0);
            analogWrite(m.lpwm, 0);
            digitalWrite(m.en, LOW);   // disable — true off
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

    MotorPins left_, right_, turn_;
    MotorTuning tuning_;
    float target_ = 0.0f;
    float output_ = 0.0f;
    float turnTarget_ = 0.0f;
    float turnOutput_ = 0.0f;
    uint32_t lastUpdateMs_ = 0;
};
