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
#include <memory>
#include "MotorController.hpp"
#include "SensorHub.hpp"
#include "FirmwareConfig.hpp"

// ---- Globals -----------------------------------------------
struct RuntimeConfig {
    MotorPins left{fwcfg::L_RPWM, fwcfg::L_LPWM, fwcfg::L_EN};
    MotorPins right{fwcfg::R_RPWM, fwcfg::R_LPWM, fwcfg::R_EN};
    uint8_t encLA{fwcfg::ENC_LA};
    uint8_t encLB{fwcfg::ENC_LB};
    uint8_t encRA{fwcfg::ENC_RA};
    uint8_t encRB{fwcfg::ENC_RB};
    SensorConfig sensor{
        fwcfg::VBAT_ADC_PIN,
        fwcfg::CURR_ADC_PIN,
        fwcfg::TEMP_ADC_PIN,
        fwcfg::VBAT_DIV_RATIO,
        fwcfg::CURR_ZERO_MV,
        fwcfg::CURR_SENS_MV_PER_A
    };
    uint32_t watchdogMs{fwcfg::WATCHDOG_MS};
    uint32_t telemIntervalMs{fwcfg::TELEM_INTERVAL_MS};
};

static RuntimeConfig gCfg;
static std::unique_ptr<MotorController> motors;
static std::unique_ptr<SensorHub> sensors;

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
static bool jsonTryGetInt(const char* json, const char* key, int* out) {
    const char* p = strstr(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    *out = (int)strtol(p + 1, nullptr, 10);
    return true;
}

static bool jsonTryGetFloat(const char* json, const char* key, float* out) {
    const char* p = strstr(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    *out = strtof(p + 1, nullptr);
    return true;
}

static uint8_t toPin(int v, uint8_t fallback) {
    if (v < 0 || v > 255) return fallback;
    return (uint8_t)v;
}

static void applyRuntimeConfig(const RuntimeConfig& cfg) {
    if (motors) motors->stop();

    gCfg = cfg;
    motors = std::make_unique<MotorController>(gCfg.left, gCfg.right);
    sensors = std::make_unique<SensorHub>(
        gCfg.encLA, gCfg.encLB, gCfg.encRA, gCfg.encRB, gCfg.sensor);

    motors->begin();
    sensors->begin();
}

static void emitConfig() {
    char buf[320];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"fw_cfg\","
        "\"l_rpwm\":%u,\"l_lpwm\":%u,\"l_en\":%u,"
        "\"r_rpwm\":%u,\"r_lpwm\":%u,\"r_en\":%u,"
        "\"enc_la\":%u,\"enc_lb\":%u,\"enc_ra\":%u,\"enc_rb\":%u,"
        "\"vbat_adc\":%u,\"curr_adc\":%u,\"temp_adc\":%u,"
        "\"vbat_div\":%.3f,\"curr_zero_mv\":%.1f,\"curr_sens_mv_per_a\":%.1f,"
        "\"watchdog_ms\":%lu,\"telem_ms\":%lu}",
        gCfg.left.rpwm, gCfg.left.lpwm, gCfg.left.en,
        gCfg.right.rpwm, gCfg.right.lpwm, gCfg.right.en,
        gCfg.encLA, gCfg.encLB, gCfg.encRA, gCfg.encRB,
        gCfg.sensor.vbatAdcPin, gCfg.sensor.currAdcPin, gCfg.sensor.tempAdcPin,
        gCfg.sensor.vbatDivRatio, gCfg.sensor.currZeroMv, gCfg.sensor.currSensMvPerA,
        (unsigned long)gCfg.watchdogMs, (unsigned long)gCfg.telemIntervalMs);
    Serial.println(buf);
}

// ---- Process one complete JSON line ------------------------
void processCommand(const char* line) {
    if (jsonHasKey(line, "\"stop\"")) {
        motors->stop();
        lastDriveMs = millis();  // reset watchdog
        return;
    }
    if (jsonHasKey(line, "\"drive\"")) {
        float l = jsonGetFloat(line, "\"l\"");
        float r = jsonGetFloat(line, "\"r\"");
        motors->drive(l, r);
        lastDriveMs = millis();
        return;
    }
    if (jsonHasKey(line, "\"sensor_req\"")) {
        sensors->update();
        char buf[200];
        sensors->toJson(buf, sizeof(buf));
        Serial.println(buf);
        return;
    }
    if (jsonHasKey(line, "\"enc_reset\"")) {
        sensors->resetEncoders();
        return;
    }
    if (jsonHasKey(line, "\"fw_cfg_get\"")) {
        emitConfig();
        return;
    }
    if (jsonHasKey(line, "\"fw_cfg\"")) {
        RuntimeConfig cfg = gCfg;
        int vi = 0;
        float vf = 0.f;

        if (jsonTryGetInt(line, "\"l_rpwm\"", &vi)) cfg.left.rpwm = toPin(vi, cfg.left.rpwm);
        if (jsonTryGetInt(line, "\"l_lpwm\"", &vi)) cfg.left.lpwm = toPin(vi, cfg.left.lpwm);
        if (jsonTryGetInt(line, "\"l_en\"", &vi)) cfg.left.en = toPin(vi, cfg.left.en);
        if (jsonTryGetInt(line, "\"r_rpwm\"", &vi)) cfg.right.rpwm = toPin(vi, cfg.right.rpwm);
        if (jsonTryGetInt(line, "\"r_lpwm\"", &vi)) cfg.right.lpwm = toPin(vi, cfg.right.lpwm);
        if (jsonTryGetInt(line, "\"r_en\"", &vi)) cfg.right.en = toPin(vi, cfg.right.en);

        if (jsonTryGetInt(line, "\"enc_la\"", &vi)) cfg.encLA = toPin(vi, cfg.encLA);
        if (jsonTryGetInt(line, "\"enc_lb\"", &vi)) cfg.encLB = toPin(vi, cfg.encLB);
        if (jsonTryGetInt(line, "\"enc_ra\"", &vi)) cfg.encRA = toPin(vi, cfg.encRA);
        if (jsonTryGetInt(line, "\"enc_rb\"", &vi)) cfg.encRB = toPin(vi, cfg.encRB);

        if (jsonTryGetInt(line, "\"vbat_adc\"", &vi)) cfg.sensor.vbatAdcPin = toPin(vi, cfg.sensor.vbatAdcPin);
        if (jsonTryGetInt(line, "\"curr_adc\"", &vi)) cfg.sensor.currAdcPin = toPin(vi, cfg.sensor.currAdcPin);
        if (jsonTryGetInt(line, "\"temp_adc\"", &vi)) cfg.sensor.tempAdcPin = toPin(vi, cfg.sensor.tempAdcPin);

        if (jsonTryGetFloat(line, "\"vbat_div\"", &vf)) cfg.sensor.vbatDivRatio = vf;
        if (jsonTryGetFloat(line, "\"curr_zero_mv\"", &vf)) cfg.sensor.currZeroMv = vf;
        if (jsonTryGetFloat(line, "\"curr_sens_mv_per_a\"", &vf)) cfg.sensor.currSensMvPerA = vf;

        if (jsonTryGetInt(line, "\"watchdog_ms\"", &vi) && vi >= 0) cfg.watchdogMs = (uint32_t)vi;
        if (jsonTryGetInt(line, "\"telem_ms\"", &vi) && vi >= 0) cfg.telemIntervalMs = (uint32_t)vi;

        applyRuntimeConfig(cfg);
        emitConfig();
        return;
    }
}

// ---- Arduino setup -----------------------------------------
void setup() {
    Serial.begin(115200);  // USB CDC to Pi
    while (!Serial && millis() < 3000) {}  // wait up to 3 s

    applyRuntimeConfig(gCfg);

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
    if ((now - lastDriveMs) > gCfg.watchdogMs) {
        motors->stop();
        lastDriveMs = now;  // prevent spamming
    }

    // --- Auto-telemetry -------------------------------------
    if (gCfg.telemIntervalMs > 0 && (now - lastTelemMs) >= gCfg.telemIntervalMs) {
        sensors->update();
        char buf[200];
        sensors->toJson(buf, sizeof(buf));
        Serial.println(buf);
        lastTelemMs = now;
    }
}
