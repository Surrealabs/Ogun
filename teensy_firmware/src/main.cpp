// ============================================================
//  Rover Teensy Firmware — Motor + Sensor Hub
//  Protocol: newline-terminated JSON over USB Serial (115200)
//
//  Pi → Teensy:
//    {"cmd":"drive","l":0.5,"r":-0.3}
//    {"cmd":"stop"}
//    {"cmd":"sensor_req"}
//    {"cmd":"enc_reset"}
//
//  Teensy → Pi (on sensor_req or every TELEM_INTERVAL_MS):
//    {"type":"sensors","enc_l":123,"enc_r":456,
//     "volt":12.4,"curr":2.1,"temp":38.5}
// ============================================================
#include <Arduino.h>
#include "MotorController.hpp"
#include "SensorHub.hpp"

// ---- Pin Assignments ---------------------------------------
// BTS7960 LEFT motor (adjust to your wiring)
constexpr uint8_t L_RPWM = 2;
constexpr uint8_t L_LPWM = 3;
constexpr uint8_t L_EN   = 4;

// BTS7960 RIGHT motor
constexpr uint8_t R_RPWM = 5;
constexpr uint8_t R_LPWM = 6;
constexpr uint8_t R_EN   = 7;

// Encoder pins (must support interrupts on Teensy 4.x — all digital pins do)
constexpr uint8_t ENC_LA = 8,  ENC_LB = 9;
constexpr uint8_t ENC_RA = 10, ENC_RB = 11;

// Watchdog: if no drive command for WATCHDOG_MS, stop motors
constexpr uint32_t WATCHDOG_MS    = 500;
// How often to auto-send telemetry (ms) — 0 = only on "sensor_req"
constexpr uint32_t TELEM_INTERVAL_MS = 100;

// ---- Globals -----------------------------------------------
MotorController motors(
    {L_RPWM, L_LPWM, L_EN},
    {R_RPWM, R_LPWM, R_EN}
);
SensorHub sensors(ENC_LA, ENC_LB, ENC_RA, ENC_RB);

static uint32_t lastDriveMs  = 0;
static uint32_t lastTelemMs  = 0;
static char     rxBuf[256];
static uint8_t  rxIdx = 0;

// ---- Simple JSON helpers (no heap) -------------------------
static float jsonGetFloat(const char* json, const char* key) {
    const char* p = strstr(json, key);
    if (!p) return 0.f;
    p = strchr(p, ':');
    if (!p) return 0.f;
    return strtof(p + 1, nullptr);
}
static bool jsonHasKey(const char* json, const char* key) {
    return strstr(json, key) != nullptr;
}

// ---- Process one complete JSON line ------------------------
void processCommand(const char* line) {
    if (jsonHasKey(line, "\"stop\"")) {
        motors.stop();
        lastDriveMs = millis();  // reset watchdog
        return;
    }
    if (jsonHasKey(line, "\"drive\"")) {
        float l = jsonGetFloat(line, "\"l\"");
        float r = jsonGetFloat(line, "\"r\"");
        motors.drive(l, r);
        lastDriveMs = millis();
        return;
    }
    if (jsonHasKey(line, "\"sensor_req\"")) {
        sensors.update();
        char buf[200];
        sensors.toJson(buf, sizeof(buf));
        Serial.println(buf);
        return;
    }
    if (jsonHasKey(line, "\"enc_reset\"")) {
        sensors.resetEncoders();
        return;
    }
}

// ---- Arduino setup -----------------------------------------
void setup() {
    Serial.begin(115200);  // USB CDC to Pi
    while (!Serial && millis() < 3000) {}  // wait up to 3 s

    motors.begin();
    sensors.begin();

    lastDriveMs = millis();
    lastTelemMs = millis();

    Serial.println("{\"type\":\"boot\",\"msg\":\"rover-teensy-ready\"}");
}

// ---- Arduino loop ------------------------------------------
void loop() {
    uint32_t now = millis();

    // --- Read serial input (byte by byte, parse on '\n') ---
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (rxIdx > 0) {
                rxBuf[rxIdx] = '\0';
                processCommand(rxBuf);
                rxIdx = 0;
            }
        } else if (rxIdx < (uint8_t)(sizeof(rxBuf) - 1)) {
            rxBuf[rxIdx++] = c;
        } else {
            rxIdx = 0;  // overflow — discard
        }
    }

    // --- Watchdog: stop motors if no drive cmd received ----
    if ((now - lastDriveMs) > WATCHDOG_MS) {
        motors.stop();
        lastDriveMs = now;  // prevent spamming
    }

    // --- Auto-telemetry -------------------------------------
    if (TELEM_INTERVAL_MS > 0 && (now - lastTelemMs) >= TELEM_INTERVAL_MS) {
        sensors.update();
        char buf[200];
        sensors.toJson(buf, sizeof(buf));
        Serial.println(buf);
        lastTelemMs = now;
    }
}
