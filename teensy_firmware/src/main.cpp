// ============================================================
//  Rover Teensy Firmware — Motor + Sensor Hub
//  Protocol: newline-terminated JSON over USB Serial (115200)
//
//  Pi → Teensy:
//    {"cmd":"drive","l":0.5,"r":-0.3}
//    {"cmd":"stop"}
//    {"cmd":"sensor_req"}
//    {"cmd":"enc_reset"}
//    {"cmd":"bootloader"}
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

extern "C" void _reboot_Teensyduino_(void);

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
        fwcfg::CURR_L_ADC_PIN,
        fwcfg::CURR_R_ADC_PIN,
        fwcfg::TEMP_ADC_PIN,
        fwcfg::VBAT_DIV_RATIO,
        fwcfg::CURR_ZERO_MV,
        fwcfg::CURR_SENS_MV_PER_A
    };
    uint32_t watchdogMs{fwcfg::WATCHDOG_MS};
    uint32_t telemIntervalMs{fwcfg::TELEM_INTERVAL_MS};
    MotorTuning motorTuning{
        fwcfg::DRIVE_MAX_FWD,
        fwcfg::DRIVE_MAX_REV,
        fwcfg::TURN_MAX,
        fwcfg::THROTTLE_EXPO,
        fwcfg::TURN_EXPO,
        fwcfg::ACCEL_UP_PER_S,
        fwcfg::ACCEL_DOWN_PER_S
    };
    float lowVoltageCutoff{fwcfg::LOW_VOLTAGE_CUTOFF};
    float lowVoltageResume{fwcfg::LOW_VOLTAGE_RESUME};
    float inputDeadband{fwcfg::INPUT_DEADBAND};
    bool  requireArm{fwcfg::REQUIRE_ARM};
};

static RuntimeConfig gCfg;
static std::unique_ptr<MotorController> motors;
static std::unique_ptr<SensorHub> sensors;

static uint32_t lastDriveMs  = 0;
static uint32_t lastTelemMs  = 0;
static char     rxBuf[256];
static uint8_t  rxIdx = 0;

// ---- Safety state ------------------------------------------
static bool     armed         = false;  // motors disabled until arm cmd
static bool     estopped      = false;  // emergency stop latched
static bool     lowVoltLatch  = false;  // battery too low
static uint32_t watchdogTrips = 0;      // how many times watchdog fired
static uint32_t bootMs        = 0;      // millis() at boot

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

static float clampFloat(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Apply deadband: values within +/- deadband snap to zero
static float applyDeadband(float v, float deadband) {
    if (fabsf(v) < deadband) return 0.0f;
    return v;
}

// Returns true if motors are allowed to run
static bool motorsAllowed() {
    if (estopped) return false;
    if (lowVoltLatch) return false;
    if (gCfg.requireArm && !armed) return false;
    return true;
}

static void forceStop() {
    if (motors) {
        motors->stop();
        motors->enable(false);
    }
}

static void applyRuntimeConfig(const RuntimeConfig& cfg) {
    if (motors) motors->stop();

    gCfg = cfg;
    motors = std::make_unique<MotorController>(gCfg.left, gCfg.right, gCfg.motorTuning);
    sensors = std::make_unique<SensorHub>(
        gCfg.encLA, gCfg.encLB, gCfg.encRA, gCfg.encRB, gCfg.sensor);

    motors->begin();
    sensors->begin();
}

static void emitConfig() {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"fw_cfg\","
        "\"l_rpwm\":%u,\"l_lpwm\":%u,\"l_en\":%u,"
        "\"r_rpwm\":%u,\"r_lpwm\":%u,\"r_en\":%u,"
        "\"enc_la\":%u,\"enc_lb\":%u,\"enc_ra\":%u,\"enc_rb\":%u,"
        "\"vbat_adc\":%u,\"curr_l_adc\":%u,\"curr_r_adc\":%u,\"temp_adc\":%u,"
        "\"vbat_div\":%.3f,\"curr_zero_mv\":%.1f,\"curr_sens_mv_per_a\":%.1f,"
        "\"watchdog_ms\":%lu,\"telem_ms\":%lu,"
        "\"drive_max_fwd\":%.3f,\"drive_max_rev\":%.3f,\"turn_max\":%.3f,"
        "\"throttle_expo\":%.3f,\"turn_expo\":%.3f,"
        "\"accel_up_per_s\":%.3f,\"accel_down_per_s\":%.3f,"
        "\"low_volt_cutoff\":%.2f,\"low_volt_resume\":%.2f,"
        "\"input_deadband\":%.3f,\"require_arm\":%s,"
        "\"armed\":%s,\"estopped\":%s,\"low_volt_latch\":%s,"
        "\"watchdog_trips\":%lu}",
        gCfg.left.rpwm, gCfg.left.lpwm, gCfg.left.en,
        gCfg.right.rpwm, gCfg.right.lpwm, gCfg.right.en,
        gCfg.encLA, gCfg.encLB, gCfg.encRA, gCfg.encRB,
        gCfg.sensor.vbatAdcPin, gCfg.sensor.currLAdcPin, gCfg.sensor.currRAdcPin, gCfg.sensor.tempAdcPin,
        gCfg.sensor.vbatDivRatio, gCfg.sensor.currZeroMv, gCfg.sensor.currSensMvPerA,
        (unsigned long)gCfg.watchdogMs, (unsigned long)gCfg.telemIntervalMs,
        gCfg.motorTuning.maxForward, gCfg.motorTuning.maxReverse, gCfg.motorTuning.maxTurn,
        gCfg.motorTuning.throttleExpo, gCfg.motorTuning.turnExpo,
        gCfg.motorTuning.accelUpPerSec, gCfg.motorTuning.accelDownPerSec,
        gCfg.lowVoltageCutoff, gCfg.lowVoltageResume,
        gCfg.inputDeadband, gCfg.requireArm ? "true" : "false",
        armed ? "true" : "false", estopped ? "true" : "false",
        lowVoltLatch ? "true" : "false",
        (unsigned long)watchdogTrips);
    Serial.println(buf);
}

// ---- Process one complete JSON line ------------------------
void processCommand(const char* line) {
    // --- Emergency stop (latching — requires explicit clear) ---
    if (jsonHasKey(line, "\"estop\"")) {
        estopped = true;
        forceStop();
        Serial.println("{\"type\":\"estop_ack\",\"estopped\":true}");
        return;
    }
    // --- Clear emergency stop ---
    if (jsonHasKey(line, "\"estop_clear\"")) {
        estopped = false;
        Serial.println("{\"type\":\"estop_ack\",\"estopped\":false}");
        return;
    }
    // --- Arm motors (must be sent before driving) ---
    if (jsonHasKey(line, "\"arm\"")) {
        armed = true;
        lastDriveMs = millis();  // reset watchdog on arm
        Serial.println("{\"type\":\"arm_ack\",\"armed\":true}");
        return;
    }
    // --- Disarm motors ---
    if (jsonHasKey(line, "\"disarm\"")) {
        armed = false;
        forceStop();
        Serial.println("{\"type\":\"arm_ack\",\"armed\":false}");
        return;
    }

    if (jsonHasKey(line, "\"stop\"")) {
        motors->stop();
        lastDriveMs = millis();  // reset watchdog
        return;
    }
    if (jsonHasKey(line, "\"drive\"")) {
        if (!motorsAllowed()) {
            motors->stop();
            lastDriveMs = millis();
            return;
        }
        float l = applyDeadband(jsonGetFloat(line, "\"l\""), gCfg.inputDeadband);
        float r = applyDeadband(jsonGetFloat(line, "\"r\""), gCfg.inputDeadband);
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
    if (jsonHasKey(line, "\"bootloader\"")) {
        Serial.println("{\"type\":\"bootloader\",\"ok\":true}");
        Serial.flush();
        delay(20);
        _reboot_Teensyduino_();
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
        if (jsonTryGetInt(line, "\"curr_l_adc\"", &vi)) cfg.sensor.currLAdcPin = toPin(vi, cfg.sensor.currLAdcPin);
        if (jsonTryGetInt(line, "\"curr_r_adc\"", &vi)) cfg.sensor.currRAdcPin = toPin(vi, cfg.sensor.currRAdcPin);
        if (jsonTryGetInt(line, "\"temp_adc\"", &vi)) cfg.sensor.tempAdcPin = toPin(vi, cfg.sensor.tempAdcPin);

        if (jsonTryGetFloat(line, "\"vbat_div\"", &vf)) cfg.sensor.vbatDivRatio = vf;
        if (jsonTryGetFloat(line, "\"curr_zero_mv\"", &vf)) cfg.sensor.currZeroMv = vf;
        if (jsonTryGetFloat(line, "\"curr_sens_mv_per_a\"", &vf)) cfg.sensor.currSensMvPerA = vf;

        if (jsonTryGetInt(line, "\"watchdog_ms\"", &vi) && vi >= 0) cfg.watchdogMs = (uint32_t)vi;
        if (jsonTryGetInt(line, "\"telem_ms\"", &vi) && vi >= 0) cfg.telemIntervalMs = (uint32_t)vi;

        if (jsonTryGetFloat(line, "\"drive_max_fwd\"", &vf)) cfg.motorTuning.maxForward = clampFloat(vf, 0.0f, 1.0f);
        if (jsonTryGetFloat(line, "\"drive_max_rev\"", &vf)) cfg.motorTuning.maxReverse = clampFloat(vf, 0.0f, 1.0f);
        if (jsonTryGetFloat(line, "\"turn_max\"", &vf)) cfg.motorTuning.maxTurn = clampFloat(vf, 0.0f, 1.0f);
        if (jsonTryGetFloat(line, "\"throttle_expo\"", &vf)) cfg.motorTuning.throttleExpo = clampFloat(vf, 0.2f, 4.0f);
        if (jsonTryGetFloat(line, "\"turn_expo\"", &vf)) cfg.motorTuning.turnExpo = clampFloat(vf, 0.2f, 4.0f);
        if (jsonTryGetFloat(line, "\"accel_up_per_s\"", &vf)) cfg.motorTuning.accelUpPerSec = clampFloat(vf, 0.05f, 20.0f);
        if (jsonTryGetFloat(line, "\"accel_down_per_s\"", &vf)) cfg.motorTuning.accelDownPerSec = clampFloat(vf, 0.05f, 30.0f);

        if (jsonTryGetFloat(line, "\"low_volt_cutoff\"", &vf)) cfg.lowVoltageCutoff = clampFloat(vf, 0.0f, 30.0f);
        if (jsonTryGetFloat(line, "\"low_volt_resume\"", &vf)) cfg.lowVoltageResume = clampFloat(vf, 0.0f, 30.0f);
        if (jsonTryGetFloat(line, "\"input_deadband\"", &vf)) cfg.inputDeadband = clampFloat(vf, 0.0f, 0.3f);
        if (jsonTryGetInt(line, "\"require_arm\"", &vi)) cfg.requireArm = (vi != 0);

        applyRuntimeConfig(cfg);
        emitConfig();
        return;
    }
}

// ---- Arduino setup -----------------------------------------
void setup() {
    Serial.begin(115200);  // USB CDC to Pi
    while (!Serial && millis() < 3000) {}  // wait up to 3 s

    armed = !fwcfg::REQUIRE_ARM;  // start disarmed if arm required
    estopped = false;
    lowVoltLatch = false;
    watchdogTrips = 0;

    applyRuntimeConfig(gCfg);

    // Motors start disabled until armed
    if (!armed) motors->enable(false);

    bootMs = millis();
    lastDriveMs = millis();
    lastTelemMs = millis();

    Serial.println("{\"type\":\"boot\",\"msg\":\"rover-teensy-ready\",\"require_arm\":true}");
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

    // --- Low-voltage cutoff (hysteresis) --------------------
    if (gCfg.lowVoltageCutoff > 0.0f) {
        float v = sensors->volt();
        // Only check after we've had at least one sensor reading (voltage > 0 means battery detected)
        if (v > 1.0f) {
            if (!lowVoltLatch && v < gCfg.lowVoltageCutoff) {
                lowVoltLatch = true;
                forceStop();
                Serial.println("{\"type\":\"safety\",\"event\":\"low_voltage\",\"volt\":" + String(v, 2) + "}");
            } else if (lowVoltLatch && v > gCfg.lowVoltageResume) {
                lowVoltLatch = false;
                Serial.println("{\"type\":\"safety\",\"event\":\"voltage_ok\",\"volt\":" + String(v, 2) + "}");
            }
        }
    }

    // --- Watchdog: stop motors if no drive cmd received ----
    if ((now - lastDriveMs) > gCfg.watchdogMs) {
        motors->stop();
        if (armed) {
            watchdogTrips++;
            // Report watchdog trip once (not every loop)
            static uint32_t lastWdReportMs = 0;
            if ((now - lastWdReportMs) > 2000) {
                char wdBuf[100];
                snprintf(wdBuf, sizeof(wdBuf),
                    "{\"type\":\"safety\",\"event\":\"watchdog\",\"trips\":%lu}",
                    (unsigned long)watchdogTrips);
                Serial.println(wdBuf);
                lastWdReportMs = now;
            }
        }
        lastDriveMs = now;  // prevent spamming stop()
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
