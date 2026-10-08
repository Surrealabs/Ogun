// ============================================================
//  Rover Teensy Firmware — Motor + Sensor Hub
//  Protocol: newline-terminated JSON over USB Serial (115200)
//
//  Pi → Teensy:
//    {"cmd":"arm"}            start a session (opens the watchdog window)
//    {"cmd":"drive","l":0.5,"r":-0.3,"t":0}   feeds the watchdog
//    {"cmd":"hb"}             operator heartbeat, feeds the watchdog
//    {"cmd":"stop"} {"cmd":"disarm"} {"cmd":"estop"} {"cmd":"estop_clear"}
//    {"cmd":"sensor_req"}     does NOT feed the watchdog
//    {"cmd":"enc_reset"} {"cmd":"bootloader"} {"cmd":"pin_diag"}
//    {"cmd":"pin_set","pin":30,"val":1}   allowlisted, non-motor pins only
//    {"cmd":"fw_cfg",...} {"cmd":"fw_cfg_get"}
//
//  Watchdog (owner rulings 2026-10-07, see SafetyLogic.hpp): no
//  drive/hb for watchdog_ms (clamped 100..1000) while armed → hard
//  stop + disarm, latched until the next {"cmd":"arm"}. Emits
//    {"type":"safety","event":"watchdog","action":"disarm",...}
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
#include "JsonLite.hpp"
#include "SafetyLogic.hpp"

static_assert(fwcfg::WATCHDOG_MS >= safety::WATCHDOG_MIN_MS &&
              fwcfg::WATCHDOG_MS <= safety::WATCHDOG_MAX_MS,
              "ROVER_WATCHDOG_MS must be within the clamp [100, 1000] ms");

extern "C" void _reboot_Teensyduino_(void);

// ---- Globals -----------------------------------------------
struct RuntimeConfig {
    MotorPins left{fwcfg::L_RPWM, fwcfg::L_LPWM, fwcfg::L_EN};
    MotorPins right{fwcfg::R_RPWM, fwcfg::R_LPWM, fwcfg::R_EN};
    MotorPins turn{fwcfg::T_RPWM, fwcfg::T_LPWM, fwcfg::T_EN};
    uint8_t encLA{fwcfg::ENC_LA};
    uint8_t encLB{fwcfg::ENC_LB};
    uint8_t encRA{fwcfg::ENC_RA};
    uint8_t encRB{fwcfg::ENC_RB};
    SensorConfig sensor{
        fwcfg::VBAT_ADC_PIN,
        fwcfg::CURR_L_ADC_PIN,
        fwcfg::CURR_R_ADC_PIN,
        fwcfg::CURR_T_ADC_PIN,
        fwcfg::TEMP_ADC_PIN,
        fwcfg::VBAT_DIV_RATIO,
        fwcfg::CURR_ZERO_MV,
        fwcfg::CURR_SENS_MV_PER_A
    };
    uint32_t watchdogMs{fwcfg::WATCHDOG_MS};
    uint32_t telemIntervalMs{fwcfg::TELEM_INTERVAL_MS};
    MotorTuning motorTuning{
        fwcfg::MAX_PWM,
        fwcfg::MIN_PWM,
        fwcfg::RAMP_SEC,
        false,           // invertLeft
        false,           // invertRight
        fwcfg::TURN_MAX_PWM,
        fwcfg::INVERT_TURN,
        fwcfg::TURN_SLOWDOWN,
        fwcfg::TURN_RAMP_SEC
    };
    float lowVoltageCutoff{fwcfg::LOW_VOLTAGE_CUTOFF};
    float lowVoltageResume{fwcfg::LOW_VOLTAGE_RESUME};
    float inputDeadband{fwcfg::INPUT_DEADBAND};
    // (No requireArm: arming is always required, owner Q1. fw_cfg
    //  "require_arm" is accepted and ignored; the reply reports true.)
    // pin_set allowlist (owner Q11): bit n = pin n. EMPTY by default;
    // set with fw_cfg {"pin_allow":[30,31]}. Motor-driver pins stay
    // refused whatever this holds (SafetyLogic.hpp pinSetVerdict).
    uint64_t pinAllowMask{0};
};

static RuntimeConfig gCfg;
static std::unique_ptr<MotorController> motors;
static std::unique_ptr<SensorHub> sensors;

static uint32_t lastTelemMs  = 0;
static safety::LineReader<256> gRx;   // same 255-char line limit as before

// ---- Safety state ------------------------------------------
// gLatch.armed: motors disabled until arm cmd; gLatch.lastFeedMs:
// last operator frame; gLatch.trips: how many times the watchdog fired.
static safety::WatchdogLatch gLatch;
static bool     estopped      = false;  // emergency stop latched
static bool     lowVoltLatch  = false;  // battery too low
static uint32_t bootMs        = 0;      // millis() at boot

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
    if (!gLatch.armed) return false;   // arming is always required
    return true;
}

// Pins pin_set may NEVER write, whatever pin_allow says (owner Q11 safety
// note; review findings 3 and 6): every BTS7960 pin. RPWM/LPWM/EN, because
// writing one bypasses the hard stop; IS (current sense), because driving it
// as an output fights the board's sense output. Both the runtime assignment
// (what the code drives now) AND the compile-time wiring, so moving a role
// with fw_cfg cannot free its wired pin. The host test (firmware_sim_test)
// calls this same function, so trimming it fails the test.
static constexpr size_t NEVER_ALLOW_PIN_COUNT = 24;
static void buildNeverAllowPins(uint8_t (&out)[NEVER_ALLOW_PIN_COUNT]) {
    const uint8_t pins[] = {
        gCfg.left.rpwm,  gCfg.left.lpwm,  gCfg.left.en,
        gCfg.right.rpwm, gCfg.right.lpwm, gCfg.right.en,
        gCfg.turn.rpwm,  gCfg.turn.lpwm,  gCfg.turn.en,
        gCfg.sensor.currLAdcPin, gCfg.sensor.currRAdcPin, gCfg.sensor.currTAdcPin,
        fwcfg::L_RPWM, fwcfg::L_LPWM, fwcfg::L_EN,
        fwcfg::R_RPWM, fwcfg::R_LPWM, fwcfg::R_EN,
        fwcfg::T_RPWM, fwcfg::T_LPWM, fwcfg::T_EN,
        fwcfg::CURR_L_ADC_PIN, fwcfg::CURR_R_ADC_PIN, fwcfg::CURR_T_ADC_PIN,
    };
    static_assert(sizeof(pins) == NEVER_ALLOW_PIN_COUNT, "update NEVER_ALLOW_PIN_COUNT");
    memcpy(out, pins, sizeof(pins));
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
    motors = std::make_unique<MotorController>(gCfg.left, gCfg.right, gCfg.turn, gCfg.motorTuning);
    sensors = std::make_unique<SensorHub>(
        gCfg.encLA, gCfg.encLB, gCfg.encRA, gCfg.encRB, gCfg.sensor);

    motors->begin();
    sensors->begin();
}

static void emitConfig() {
    // 648 chars at default values before pin_allow was added; a full
    // pin_allow adds up to 130. NOTE: the Pi's TeensyBridge drops any
    // line over 512 chars, so this reply only reaches a raw-serial
    // reader today (FIRMWARE_PLAN.md, "Known limits").
    char pinList[160];
    safety::formatPinList(gCfg.pinAllowMask, pinList, sizeof(pinList));
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"fw_cfg\","
        "\"l_rpwm\":%u,\"l_lpwm\":%u,\"l_en\":%u,"
        "\"r_rpwm\":%u,\"r_lpwm\":%u,\"r_en\":%u,"
        "\"t_rpwm\":%u,\"t_lpwm\":%u,\"t_en\":%u,"
        "\"enc_la\":%u,\"enc_lb\":%u,\"enc_ra\":%u,\"enc_rb\":%u,"
        "\"vbat_adc\":%u,\"curr_l_adc\":%u,\"curr_r_adc\":%u,\"curr_t_adc\":%u,\"temp_adc\":%u,"
        "\"vbat_div\":%.3f,\"curr_zero_mv\":%.1f,\"curr_sens_mv_per_a\":%.1f,"
        "\"watchdog_ms\":%lu,\"telem_ms\":%lu,"
        "\"max_pwm\":%u,\"min_pwm\":%u,\"ramp_sec\":%.3f,"
        "\"invert_left\":%d,\"invert_right\":%d,"
        "\"turn_max_pwm\":%u,\"invert_turn\":%d,"
        "\"turn_slowdown\":%.2f,\"turn_ramp_sec\":%.3f,"
        "\"low_volt_cutoff\":%.2f,\"low_volt_resume\":%.2f,"
        "\"input_deadband\":%.3f,\"require_arm\":true,"
        "\"armed\":%s,\"estopped\":%s,\"low_volt_latch\":%s,"
        "\"watchdog_trips\":%lu,\"pin_allow\":%s}",
        gCfg.left.rpwm, gCfg.left.lpwm, gCfg.left.en,
        gCfg.right.rpwm, gCfg.right.lpwm, gCfg.right.en,
        gCfg.turn.rpwm, gCfg.turn.lpwm, gCfg.turn.en,
        gCfg.encLA, gCfg.encLB, gCfg.encRA, gCfg.encRB,
        gCfg.sensor.vbatAdcPin, gCfg.sensor.currLAdcPin, gCfg.sensor.currRAdcPin,
        gCfg.sensor.currTAdcPin, gCfg.sensor.tempAdcPin,
        gCfg.sensor.vbatDivRatio, gCfg.sensor.currZeroMv, gCfg.sensor.currSensMvPerA,
        (unsigned long)gCfg.watchdogMs, (unsigned long)gCfg.telemIntervalMs,
        gCfg.motorTuning.maxPwm, gCfg.motorTuning.minPwm, gCfg.motorTuning.rampSec,
        (int)gCfg.motorTuning.invertLeft, (int)gCfg.motorTuning.invertRight,
        gCfg.motorTuning.turnMaxPwm, (int)gCfg.motorTuning.invertTurn,
        gCfg.motorTuning.turnSlowdown, gCfg.motorTuning.turnRampSec,
        gCfg.lowVoltageCutoff, gCfg.lowVoltageResume,
        gCfg.inputDeadband,
        gLatch.armed ? "true" : "false", estopped ? "true" : "false",
        lowVoltLatch ? "true" : "false",
        (unsigned long)gLatch.trips, pinList);
    Serial.println(buf);
}

// ---- Process one complete JSON line ------------------------
void processCommand(const char* line) {
    using safety::Cmd;
    const Cmd cmd = safety::classifyCommand(line);
    // Arm state and the watchdog feed live in one host-tested place
    // (SafetyLogic.hpp): drive/hb/arm feed it; arm arms; disarm and
    // estop disarm. The cases below only drive outputs and reply.
    gLatch.onCommand(cmd, millis());

    switch (cmd) {
    // --- Emergency stop (latching — requires explicit clear; also disarms) ---
    case Cmd::Estop:
        estopped = true;
        forceStop();
        Serial.println("{\"type\":\"estop_ack\",\"estopped\":true}");
        return;
    // --- Clear emergency stop (stays disarmed until arm) ---
    case Cmd::EstopClear:
        estopped = false;
        Serial.println("{\"type\":\"estop_ack\",\"estopped\":false}");
        return;
    // --- Arm motors: required before driving, and the ONLY way out
    //     of a watchdog trip. Opens a fresh watchdog window. Always
    //     starts from ZERO targets (review finding 2): whatever target
    //     was stored before must never be what the ramp heads for, and
    //     this must not depend on the host sending stop after arm. ---
    case Cmd::Arm:
        motors->stop();
        Serial.println("{\"type\":\"arm_ack\",\"armed\":true}");
        return;
    // --- Disarm motors ---
    case Cmd::Disarm:
        forceStop();
        Serial.println("{\"type\":\"arm_ack\",\"armed\":false}");
        return;
    // --- Stop: zero the outputs. Does NOT feed the watchdog: the Pi
    //     sends stop on its own (e.g. in reply to drive while not
    //     started), so it is no proof that an operator is there. ---
    case Cmd::Stop:
        motors->stop();
        return;
    // --- Drive: operator frame (fed the watchdog above) ---
    case Cmd::Drive: {
        if (!motorsAllowed()) {
            motors->stop();
            return;
        }
        float l = applyDeadband(jsonGetFloat(line, "\"l\""), gCfg.inputDeadband);
        float r = applyDeadband(jsonGetFloat(line, "\"r\""), gCfg.inputDeadband);
        float t = applyDeadband(jsonGetFloat(line, "\"t\""), gCfg.inputDeadband);
        motors->setTarget(l, r);
        motors->setTurnTarget(t);
        return;
    }
    // --- Operator heartbeat: feeds the watchdog (above), nothing else.
    //     No reply, so it can run at frame rate without serial spam. ---
    case Cmd::Hb:
        return;
    // --- Sensor request: answered, but NO LONGER feeds the watchdog
    //     (it did, so a live Pi kept a driverless rover armed). ---
    case Cmd::SensorReq: {
        sensors->update();
        char buf[200];
        sensors->toJson(buf, sizeof(buf));
        Serial.println(buf);
        return;
    }
    case Cmd::EncReset:
        sensors->resetEncoders();
        return;
    case Cmd::Bootloader:
        Serial.println("{\"type\":\"bootloader\",\"ok\":true}");
        Serial.flush();
        delay(20);
        _reboot_Teensyduino_();
        return;
    case Cmd::PinDiag: {
        // Read back actual pin states for turn motor to diagnose hardware
        char dbuf[300];
        snprintf(dbuf, sizeof(dbuf),
            "{\"type\":\"pin_diag\","
            "\"t_rpwm_pin\":%u,\"t_lpwm_pin\":%u,\"t_en_pin\":%u,"
            "\"t_en_read\":%d,"
            "\"t_rpwm_read\":%d,\"t_lpwm_read\":%d,"
            "\"l_en_pin\":%u,\"l_en_read\":%d,"
            "\"r_en_pin\":%u,\"r_en_read\":%d,"
            "\"armed\":%s,\"tick_running\":%s}",
            gCfg.turn.rpwm, gCfg.turn.lpwm, gCfg.turn.en,
            digitalRead(gCfg.turn.en),
            digitalRead(gCfg.turn.rpwm), digitalRead(gCfg.turn.lpwm),
            gCfg.left.en, digitalRead(gCfg.left.en),
            gCfg.right.en, digitalRead(gCfg.right.en),
            gLatch.armed ? "true" : "false",
            (gLatch.armed && !estopped && !lowVoltLatch) ? "true" : "false");
        Serial.println(dbuf);
        return;
    }
    // Pin write: {"cmd":"pin_set","pin":30,"val":1}
    // Owner Q11: allowed only for pins on gCfg.pinAllowMask (empty by
    // default, set via fw_cfg "pin_allow"). Every BTS7960 pin is refused
    // ALWAYS (buildNeverAllowPins). Arming state does not matter.
    // Owner 2026-10-07: serial/SSH only for now; the Pi must stop
    // forwarding pin_set/pin_allow from network clients (Step 2).
    case Cmd::PinSet: {
        int pin = -1, val = -1;
        char pbuf[120];
        if (!(jsonTryGetInt(line, "\"pin\"", &pin) && jsonTryGetInt(line, "\"val\"", &val))) {
            Serial.println("{\"type\":\"pin_set\",\"ok\":false,\"reason\":\"bad_args\"}");
            return;
        }
        uint8_t neverAllow[NEVER_ALLOW_PIN_COUNT];
        buildNeverAllowPins(neverAllow);
        const safety::PinVerdict verdict = safety::pinSetVerdict(
            pin, gCfg.pinAllowMask, neverAllow, NEVER_ALLOW_PIN_COUNT);
        if (verdict != safety::PinVerdict::Ok) {
            snprintf(pbuf, sizeof(pbuf),
                "{\"type\":\"pin_set\",\"pin\":%d,\"ok\":false,\"reason\":\"%s\"}",
                pin, safety::pinVerdictName(verdict));
            Serial.println(pbuf);
            return;
        }
        pinMode((uint8_t)pin, OUTPUT);
        digitalWrite((uint8_t)pin, val ? HIGH : LOW);
        snprintf(pbuf, sizeof(pbuf),
            "{\"type\":\"pin_set\",\"pin\":%d,\"val\":%d,\"read\":%d,\"ok\":true}",
            pin, val, digitalRead((uint8_t)pin));
        Serial.println(pbuf);
        return;
    }
    case Cmd::FwCfgGet:
        emitConfig();
        return;
    case Cmd::FwCfg: {
        RuntimeConfig cfg = gCfg;
        int vi = 0;
        float vf = 0.f;

        if (jsonTryGetInt(line, "\"l_rpwm\"", &vi)) cfg.left.rpwm = toPin(vi, cfg.left.rpwm);
        if (jsonTryGetInt(line, "\"l_lpwm\"", &vi)) cfg.left.lpwm = toPin(vi, cfg.left.lpwm);
        if (jsonTryGetInt(line, "\"l_en\"", &vi)) cfg.left.en = toPin(vi, cfg.left.en);
        if (jsonTryGetInt(line, "\"r_rpwm\"", &vi)) cfg.right.rpwm = toPin(vi, cfg.right.rpwm);
        if (jsonTryGetInt(line, "\"r_lpwm\"", &vi)) cfg.right.lpwm = toPin(vi, cfg.right.lpwm);
        if (jsonTryGetInt(line, "\"r_en\"", &vi)) cfg.right.en = toPin(vi, cfg.right.en);
        if (jsonTryGetInt(line, "\"t_rpwm\"", &vi)) cfg.turn.rpwm = toPin(vi, cfg.turn.rpwm);
        if (jsonTryGetInt(line, "\"t_lpwm\"", &vi)) cfg.turn.lpwm = toPin(vi, cfg.turn.lpwm);
        if (jsonTryGetInt(line, "\"t_en\"", &vi)) cfg.turn.en = toPin(vi, cfg.turn.en);

        if (jsonTryGetInt(line, "\"enc_la\"", &vi)) cfg.encLA = toPin(vi, cfg.encLA);
        if (jsonTryGetInt(line, "\"enc_lb\"", &vi)) cfg.encLB = toPin(vi, cfg.encLB);
        if (jsonTryGetInt(line, "\"enc_ra\"", &vi)) cfg.encRA = toPin(vi, cfg.encRA);
        if (jsonTryGetInt(line, "\"enc_rb\"", &vi)) cfg.encRB = toPin(vi, cfg.encRB);

        if (jsonTryGetInt(line, "\"vbat_adc\"", &vi)) cfg.sensor.vbatAdcPin = toPin(vi, cfg.sensor.vbatAdcPin);
        if (jsonTryGetInt(line, "\"curr_l_adc\"", &vi)) cfg.sensor.currLAdcPin = toPin(vi, cfg.sensor.currLAdcPin);
        if (jsonTryGetInt(line, "\"curr_r_adc\"", &vi)) cfg.sensor.currRAdcPin = toPin(vi, cfg.sensor.currRAdcPin);
        if (jsonTryGetInt(line, "\"curr_t_adc\"", &vi)) cfg.sensor.currTAdcPin = toPin(vi, cfg.sensor.currTAdcPin);
        if (jsonTryGetInt(line, "\"temp_adc\"", &vi)) cfg.sensor.tempAdcPin = toPin(vi, cfg.sensor.tempAdcPin);

        if (jsonTryGetFloat(line, "\"vbat_div\"", &vf)) cfg.sensor.vbatDivRatio = vf;
        if (jsonTryGetFloat(line, "\"curr_zero_mv\"", &vf)) cfg.sensor.currZeroMv = vf;
        if (jsonTryGetFloat(line, "\"curr_sens_mv_per_a\"", &vf)) cfg.sensor.currSensMvPerA = vf;

        // Owner Q2 + gap rule: no client can set the window outside [100, 1000] ms.
        if (jsonTryGetInt(line, "\"watchdog_ms\"", &vi)) cfg.watchdogMs = safety::clampWatchdogMs(vi);
        if (jsonTryGetInt(line, "\"telem_ms\"", &vi) && vi >= 0) cfg.telemIntervalMs = (uint32_t)vi;

        if (jsonTryGetFloat(line, "\"drive_max_fwd\"", &vf)) {} // legacy — ignored
        if (jsonTryGetFloat(line, "\"drive_max_rev\"", &vf)) {} // legacy — ignored
        if (jsonTryGetFloat(line, "\"turn_max\"", &vf)) {} // legacy — ignored
        if (jsonTryGetFloat(line, "\"throttle_expo\"", &vf)) {} // legacy — ignored
        if (jsonTryGetFloat(line, "\"turn_expo\"", &vf)) {} // legacy — ignored
        if (jsonTryGetFloat(line, "\"accel_up_per_s\"", &vf)) {} // legacy — ignored
        if (jsonTryGetFloat(line, "\"accel_down_per_s\"", &vf)) {} // legacy — ignored

        if (jsonTryGetInt(line, "\"max_pwm\"", &vi)) cfg.motorTuning.maxPwm = (uint8_t)constrain(vi, 0, 255);
        if (jsonTryGetInt(line, "\"min_pwm\"", &vi)) cfg.motorTuning.minPwm = (uint8_t)constrain(vi, 0, 255);
        if (jsonTryGetFloat(line, "\"ramp_sec\"", &vf)) cfg.motorTuning.rampSec = clampFloat(vf, 0.05f, 30.0f);

        if (jsonTryGetInt(line, "\"invert_left\"", &vi)) cfg.motorTuning.invertLeft = (vi != 0);
        if (jsonTryGetInt(line, "\"invert_right\"", &vi)) cfg.motorTuning.invertRight = (vi != 0);
        if (jsonTryGetInt(line, "\"turn_max_pwm\"", &vi)) cfg.motorTuning.turnMaxPwm = (uint8_t)constrain(vi, 0, 255);
        if (jsonTryGetInt(line, "\"invert_turn\"", &vi)) cfg.motorTuning.invertTurn = (vi != 0);
        if (jsonTryGetFloat(line, "\"turn_slowdown\"", &vf)) cfg.motorTuning.turnSlowdown = clampFloat(vf, 0.0f, 1.0f);
        if (jsonTryGetFloat(line, "\"turn_ramp_sec\"", &vf)) cfg.motorTuning.turnRampSec = clampFloat(vf, 0.01f, 10.0f);

        if (jsonTryGetFloat(line, "\"low_volt_cutoff\"", &vf)) cfg.lowVoltageCutoff = clampFloat(vf, 0.0f, 30.0f);
        if (jsonTryGetFloat(line, "\"low_volt_resume\"", &vf)) cfg.lowVoltageResume = clampFloat(vf, 0.0f, 30.0f);
        if (jsonTryGetFloat(line, "\"input_deadband\"", &vf)) cfg.inputDeadband = clampFloat(vf, 0.0f, 0.3f);
        // "require_arm" is accepted and IGNORED (review finding 7): arming is
        // always required (owner Q1). The Pi's startup line still sends it.

        // pin_set allowlist, all or nothing: "pin_allow":[30,31] replaces
        // it, [] clears it, a malformed list leaves it unchanged.
        uint64_t allow = 0;
        if (safety::jsonTryGetPinList(line, "\"pin_allow\"", &allow)) cfg.pinAllowMask = allow;

        applyRuntimeConfig(cfg);
        emitConfig();
        return;
    }
    case Cmd::None:
        return;
    }
}

// ---- Arduino setup -----------------------------------------
void setup() {
    Serial.begin(115200);  // USB CDC to Pi
    while (!Serial && millis() < 3000) {}  // wait up to 3 s

    gLatch.armed = false;  // always start disarmed: arming is always required
    estopped = false;
    lowVoltLatch = false;
    gLatch.trips = 0;

    applyRuntimeConfig(gCfg);

    // Motors start disabled until armed
    motors->enable(false);

    bootMs = millis();
    gLatch.lastFeedMs = millis();
    lastTelemMs = millis();

    Serial.println("{\"type\":\"boot\",\"msg\":\"rover-teensy-ready\",\"require_arm\":true}");
}

// ---- Arduino loop ------------------------------------------
void loop() {
    // --- Read serial input (byte by byte, parse on '\n') ---
    // A line longer than 255 chars is now dropped whole; it used to
    // be cut and its tail run as a separate command.
    while (Serial.available()) {
        const char* line = gRx.push((char)Serial.read());
        if (line) processCommand(line);
    }

    // Sample the clock AFTER the input is processed. Sampled before
    // (as it was), a feed stamped by processCommand() a tick later
    // made (now - lastFeed) underflow to ~49 days: a false trip,
    // harmless as a coast but a spurious DISARM under the new rule.
    const uint32_t now = millis();

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

    // --- Watchdog: operator frames stopped → HARD STOP + DISARM ----
    // Owner Q1 (2026-10-07): no coast, whatever ramp_sec says. The
    // latch holds until an explicit {"cmd":"arm"}; drive frames do not
    // re-arm. One trip per arming, so the line is never rate-limited.
    if (gLatch.poll(now, gCfg.watchdogMs)) {
        forceStop();  // PWMs to 0 and every BTS7960 enable LOW
        char wdBuf[128];
        snprintf(wdBuf, sizeof(wdBuf),
            "{\"type\":\"safety\",\"event\":\"watchdog\",\"action\":\"disarm\","
            "\"trips\":%lu,\"watchdog_ms\":%lu}",
            (unsigned long)gLatch.trips, (unsigned long)gCfg.watchdogMs);
        Serial.println(wdBuf);
    }

    // --- Run motor slew every loop tick ---------------------
    // This advances the ramp smoothly regardless of command rate.
    if (gLatch.armed && motorsAllowed()) {
        motors->tick();
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
