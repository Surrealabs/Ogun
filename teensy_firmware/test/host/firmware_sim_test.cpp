// ============================================================
//  Firmware simulation test: compiles the REAL teensy_firmware
//  sources (src/main.cpp and everything it includes) against a fake
//  Arduino layer (fake_arduino/), then drives setup()/loop() with
//  serial lines and a controlled clock, and checks the recorded pin
//  writes. Covers what safety_logic_test cannot: the processCommand
//  switch, loop() ordering, forceStop(), arm zeroing targets, the
//  firmware's own never-allow pin list, and the built-in defaults.
//
//  Build and run from this directory with any C++17 compiler:
//    g++ -std=c++17 -Wall -Wextra -I fake_arduino -I ../../src firmware_sim_test.cpp -o firmware_sim_test && ./firmware_sim_test
//    cl /nologo /std:c++17 /W4 /WX /EHsc /I fake_arduino /I ..\..\src firmware_sim_test.cpp && firmware_sim_test.exe
//  fake_arduino MUST come first on the include path.
//
//  It models logic, not electricity: a pin "off" here means the
//  firmware wrote PWM 0 and EN LOW, nothing more.
// ============================================================
#include "main.cpp"   // the real firmware translation unit, statics and all

#include <cstdio>
#include <set>
#include <string>

extern "C" void _reboot_Teensyduino_(void) { fake::g_rebooted = true; }

// Owner ruling 2026-10-07: the built-in turn defaults are commit 573f8b8's
// values. Checked here (test build, no -D overrides) rather than in the
// firmware, so a deliberate -DROVER_TURN_MAX_PWM build still compiles.
static_assert(fwcfg::TURN_MAX_PWM == 90, "built-in turn_max_pwm must be 90 (573f8b8)");
static_assert(fwcfg::TURN_SLOWDOWN == 0.85f, "built-in turn_slowdown must be 0.85 (573f8b8)");
static_assert(fwcfg::TURN_RAMP_SEC == 0.35f, "built-in turn_ramp_sec must be 0.35 (573f8b8)");
static_assert(!fwcfg::INVERT_TURN, "built-in invert_turn must be false (573f8b8)");

static int gPass = 0;
static int gFail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) { ++gPass; }                                               \
        else { ++gFail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

// ---- Harness ------------------------------------------------
static void boot(uint32_t at = 0) {
    fake::reset();
    fake::g_autoTick = false;
    if (at) fake::g_millis = at; else fake::g_millis += 1000;
    Serial.in.clear();
    Serial.out.clear();
    gCfg = RuntimeConfig{};
    gRx = safety::LineReader<256>{};
    gLatch = safety::WatchdogLatch{};
    estopped = false;
    lowVoltLatch = false;
    setup();
}
static void send(const std::string& line) { Serial.feed(line + "\n"); }
static void step(uint32_t ms = 1) { for (uint32_t i = 0; i < ms; ++i) { ++fake::g_millis; loop(); } }

static bool motorOn(const MotorPins& m) {
    return fake::g_level[m.en] == HIGH && (fake::g_pwm[m.rpwm] > 0 || fake::g_pwm[m.lpwm] > 0);
}
static bool motorOff(const MotorPins& m) {
    return fake::g_level[m.en] == LOW && fake::g_pwm[m.rpwm] == 0 && fake::g_pwm[m.lpwm] == 0;
}
static bool anyMotorOn() { return motorOn(gCfg.left) || motorOn(gCfg.right) || motorOn(gCfg.turn); }
static bool allMotorsOff() { return motorOff(gCfg.left) && motorOff(gCfg.right) && motorOff(gCfg.turn); }

// Run `ms` milliseconds, sending `line` every `every` ms (empty = silence).
// Returns true if any motor output was ever on during the run.
static bool runWith(uint32_t ms, const std::string& line = "", uint32_t every = 0) {
    bool everOn = false;
    for (uint32_t i = 0; i < ms; ++i) {
        if (!line.empty() && every && (i % every) == 0) send(line);
        step(1);
        everOn = everOn || anyMotorOn();
    }
    return everOn;
}

static size_t outMark() { return Serial.out.size(); }
static std::string lastLineWith(const char* needle, size_t from = 0) {
    for (size_t i = Serial.out.size(); i-- > from;) {
        if (Serial.out[i].find(needle) != std::string::npos) return Serial.out[i];
    }
    return "";
}
static int countLinesWith(const char* needle, size_t from = 0) {
    int n = 0;
    for (size_t i = from; i < Serial.out.size(); ++i) n += Serial.out[i].find(needle) != std::string::npos;
    return n;
}
static bool has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }
static std::string fwCfgReply() {
    const size_t m = outMark();
    send("{\"cmd\":\"fw_cfg_get\"}");
    step(1);
    return lastLineWith("\"type\":\"fw_cfg\"", m);
}

static const char* kDrive05 = "{\"cmd\":\"drive\",\"l\":0.5,\"r\":0.5,\"t\":0.5}";
static const char* kHb      = "{\"cmd\":\"hb\"}";
static const char* kArm     = "{\"cmd\":\"arm\"}";

// ---- Tests --------------------------------------------------

// Owner ruling 2026-10-07 (turn defaults from commit 573f8b8) and
// review finding 7 (arming always required).
static void test_boot_defaults() {
    boot();
    CHECK(countLinesWith("rover-teensy-ready") == 1);
    CHECK(gCfg.motorTuning.turnMaxPwm == 90);
    const std::string r = fwCfgReply();
    CHECK(has(r, "\"turn_max_pwm\":90,"));
    CHECK(has(r, "\"turn_slowdown\":0.85,"));
    CHECK(has(r, "\"turn_ramp_sec\":0.350,"));
    CHECK(has(r, "\"invert_turn\":0,"));
    CHECK(has(r, "\"watchdog_ms\":500,"));
    CHECK(has(r, "\"require_arm\":true,"));
    CHECK(has(r, "\"armed\":false,"));
    CHECK(has(r, "\"pin_allow\":[]}"));
    CHECK(!gLatch.armed);
    CHECK(allMotorsOff());
    // require_arm=0 is ignored: a drive while disarmed never moves.
    send("{\"cmd\":\"fw_cfg\",\"require_arm\":0}");
    step(1);
    CHECK(has(fwCfgReply(), "\"require_arm\":true,"));
    CHECK(!runWith(1500, kDrive05, 33));
    CHECK(!gLatch.armed);
}

// The turn motor actually runs at the new ceiling: 90 PWM, not 255.
static void test_turn_ceiling() {
    boot();
    send(kArm);
    runWith(2000, "{\"cmd\":\"drive\",\"l\":0,\"r\":0,\"t\":1.0}", 33);
    const int turnPwm = fake::g_pwm[gCfg.turn.rpwm];
    CHECK(turnPwm >= 89 && turnPwm <= 90);
    CHECK(fake::g_level[gCfg.turn.en] == HIGH);
}

// Q1: the trip is an immediate hard stop (no ramp), then latched.
static void test_trip_hard_stop_and_latch() {
    boot();
    send(kArm);
    CHECK(runWith(2000, kDrive05, 33));             // driving
    const uint32_t lastFeed = gLatch.lastFeedMs;
    const size_t m = outMark();
    // Silence. Up to and including lastFeed + 500 the motors keep their
    // output (no coast); at lastFeed + 501 everything is cut at once.
    while (fake::g_millis < lastFeed + 500) step(1);
    CHECK(anyMotorOn());
    CHECK(gLatch.armed);
    step(1);
    CHECK(fake::g_millis == lastFeed + 501);
    CHECK(!gLatch.armed);
    CHECK(allMotorsOff());
    const std::string s = lastLineWith("\"type\":\"safety\"", m);
    CHECK(has(s, "\"event\":\"watchdog\""));
    CHECK(has(s, "\"action\":\"disarm\""));
    CHECK(has(s, "\"trips\":1,"));
    CHECK(has(s, "\"watchdog_ms\":500"));
    // Latched: drive and stop frames do nothing.
    CHECK(!runWith(2000, kDrive05, 33));
    CHECK(!runWith(500, "{\"cmd\":\"stop\"}", 50));
    // Bench gate (g) addition: arm ALONE (no stop, no drive) moves nothing.
    send(kArm);
    CHECK(!runWith(1000, kHb, 100));
    CHECK(gLatch.armed);
    // Fresh drive frames after arm do move it.
    CHECK(runWith(1000, kDrive05, 33));
    CHECK(gLatch.trips == 1);
}

// ramp_sec = 30 cannot stretch the stop.
static void test_trip_ignores_ramp() {
    boot();
    send("{\"cmd\":\"fw_cfg\",\"ramp_sec\":30}");
    step(1);
    send(kArm);
    CHECK(runWith(3000, "{\"cmd\":\"drive\",\"l\":1,\"r\":1,\"t\":0}", 33));
    const uint32_t lastFeed = gLatch.lastFeedMs;
    while (fake::g_millis < lastFeed + 501) step(1);
    CHECK(allMotorsOff());
    CHECK(!gLatch.armed);
}

// Review finding 2: arm always starts from zero targets.
static void test_arm_zeroes_stale_target() {
    boot();
    motors->setTarget(0.6f, 0.6f);      // a stale target, however it got there
    motors->setTurnTarget(0.6f);
    send(kArm);                          // no stop after it, unlike the Pi
    CHECK(!runWith(1500, kHb, 100));     // armed and fed, yet nothing moves
    CHECK(gLatch.armed);
}

static void test_non_operator_frames_do_not_feed() {
    const char* lines[] = { "{\"cmd\":\"sensor_req\"}", "{\"cmd\":\"stop\"}", "{\"cmd\":\"fw_cfg_get\"}" };
    for (const char* l : lines) {
        boot();
        send(kArm);
        step(1);
        runWith(1000, l, 50);
        CHECK(!gLatch.armed);
        CHECK(gLatch.trips == 1);
    }
    // hb alone keeps it armed.
    boot();
    send(kArm);
    runWith(5000, kHb, 200);
    CHECK(gLatch.armed);
    CHECK(gLatch.trips == 0);
}

// loop() samples the clock after input, and elapsed is signed: with the
// clock advancing on EVERY millis() call, a 30 Hz drive never false-trips.
static void test_clock_race_at_loop_level() {
    boot();
    send(kArm);
    step(1);
    fake::g_autoTick = true;
    const uint32_t start = fake::g_millis;
    uint32_t nextFrame = start;
    while (fake::g_millis - start < 3000) {
        if (fake::g_millis >= nextFrame) { send(kDrive05); nextFrame += 33; }
        loop();
    }
    fake::g_autoTick = false;
    CHECK(gLatch.armed);
    CHECK(gLatch.trips == 0);
}

// Review findings 3 and 6: the firmware's OWN list, every BTS7960 pin.
static void test_pin_set_never_allow() {
    boot();
    std::string all = "{\"cmd\":\"fw_cfg\",\"pin_allow\":[";
    for (int p = 0; p <= safety::PIN_SET_MAX; ++p) all += (p ? "," : "") + std::to_string(p);
    all += "]}";
    CHECK(all.size() < 255);
    send(all);
    step(1);
    CHECK(gCfg.pinAllowMask == ((1ull << 42) - 1));

    uint8_t list[NEVER_ALLOW_PIN_COUNT];
    buildNeverAllowPins(list);
    const std::set<int> refused(list, list + NEVER_ALLOW_PIN_COUNT);
    // The spec, by role (not by number): every RPWM/LPWM/EN/IS pin, runtime and wired.
    const int roles[] = {
        gCfg.left.rpwm, gCfg.left.lpwm, gCfg.left.en,
        gCfg.right.rpwm, gCfg.right.lpwm, gCfg.right.en,
        gCfg.turn.rpwm, gCfg.turn.lpwm, gCfg.turn.en,
        gCfg.sensor.currLAdcPin, gCfg.sensor.currRAdcPin, gCfg.sensor.currTAdcPin,
        fwcfg::L_RPWM, fwcfg::L_LPWM, fwcfg::L_EN, fwcfg::R_RPWM, fwcfg::R_LPWM, fwcfg::R_EN,
        fwcfg::T_RPWM, fwcfg::T_LPWM, fwcfg::T_EN,
        fwcfg::CURR_L_ADC_PIN, fwcfg::CURR_R_ADC_PIN, fwcfg::CURR_T_ADC_PIN };
    for (int r : roles) CHECK(refused.count(r) == 1);
    CHECK(refused.count(21) && refused.count(17) && refused.count(13));   // IS pins (finding 6)

    // Every listed pin is refused end to end, with no write to it.
    for (int p : refused) {
        const int writesBefore = fake::g_digitalWrites[p];
        const size_t m = outMark();
        send("{\"cmd\":\"pin_set\",\"pin\":" + std::to_string(p) + ",\"val\":1}");
        step(1);
        const std::string r = lastLineWith("\"type\":\"pin_set\"", m);
        CHECK(has(r, "\"ok\":false") && has(r, "\"reason\":\"motor_pin\""));
        CHECK(fake::g_digitalWrites[p] == writesBefore);
    }
    // A spare allowlisted pin works.
    {
        const size_t m = outMark();
        send("{\"cmd\":\"pin_set\",\"pin\":30,\"val\":1}");
        step(1);
        CHECK(has(lastLineWith("\"type\":\"pin_set\"", m), "\"ok\":true"));
        CHECK(fake::g_level[30] == HIGH && fake::g_mode[30] == OUTPUT);
    }
    // A role moved with fw_cfg: both the new pin and the wired pin are refused.
    send("{\"cmd\":\"fw_cfg\",\"l_en\":33}");
    step(1);
    for (int p : { 33, (int)fwcfg::L_EN }) {
        const size_t m = outMark();
        send("{\"cmd\":\"pin_set\",\"pin\":" + std::to_string(p) + ",\"val\":1}");
        step(1);
        CHECK(has(lastLineWith("\"type\":\"pin_set\"", m), "\"reason\":\"motor_pin\""));
    }
    // Default empty list: every pin refused.
    boot();
    {
        const size_t m = outMark();
        send("{\"cmd\":\"pin_set\",\"pin\":30,\"val\":1}");
        step(1);
        CHECK(has(lastLineWith("\"type\":\"pin_set\"", m), "\"reason\":\"not_allowed\""));
        CHECK(fake::g_digitalWrites[30] == 0);
    }
    // Arming state does not matter for an allowlisted spare pin.
    send("{\"cmd\":\"fw_cfg\",\"pin_allow\":[30]}");
    send(kArm);
    send("{\"cmd\":\"pin_set\",\"pin\":30,\"val\":1}");
    const size_t m = outMark();
    step(1);
    CHECK(has(lastLineWith("\"type\":\"pin_set\"", m), "\"ok\":true"));
}

// Review finding 5: non-numeric / non-finite values are rejected and the
// old value is kept; garbage drive values are neutral, never full reverse.
static void test_numeric_strictness() {
    boot();
    const char* badWatchdog[] = { "\"x\"", "null", "abc", "\"500\"", "" };
    for (const char* v : badWatchdog) {
        send(std::string("{\"cmd\":\"fw_cfg\",\"watchdog_ms\":") + v + "}");
        step(1);
        CHECK(gCfg.watchdogMs == 500);
    }
    const char* badFloat[] = { "\"slow\"", "nan", "inf", "-inf", "1e39" };
    for (const char* v : badFloat) {
        send(std::string("{\"cmd\":\"fw_cfg\",\"ramp_sec\":") + v + "}");
        step(1);
        CHECK(gCfg.motorTuning.rampSec == fwcfg::RAMP_SEC);
    }
    send("{\"cmd\":\"fw_cfg\",\"turn_max_pwm\":\"x\"}");
    step(1);
    CHECK(gCfg.motorTuning.turnMaxPwm == 90);
    // Valid values still apply, clamped.
    send("{\"cmd\":\"fw_cfg\",\"watchdog_ms\":5000}"); step(1); CHECK(gCfg.watchdogMs == 1000);
    send("{\"cmd\":\"fw_cfg\",\"watchdog_ms\":0}");    step(1); CHECK(gCfg.watchdogMs == 100);
    send("{\"cmd\":\"fw_cfg\",\"watchdog_ms\":750}");  step(1); CHECK(gCfg.watchdogMs == 750);
    // After a rejected value the window is the old one (500), not 100.
    boot();
    send("{\"cmd\":\"fw_cfg\",\"watchdog_ms\":null}");
    send(kArm);
    step(1);
    const uint32_t armedAt = gLatch.lastFeedMs;
    while (gLatch.armed && fake::g_millis < armedAt + 2000) step(1);
    CHECK(fake::g_millis == armedAt + 501);
    // nan/inf drive inputs are neutral (they used to ramp to full reverse).
    for (const char* v : { "nan", "inf", "-inf" }) {
        boot();
        send(kArm);
        const std::string frame = std::string("{\"cmd\":\"drive\",\"l\":") + v + ",\"r\":" + v + ",\"t\":" + v + "}";
        CHECK(!runWith(3000, frame, 33));
        CHECK(gLatch.armed);                // still an operator frame: it feeds
    }
}

// Review finding 4: a partial line run into the next is not a command.
static void test_concatenated_partial_line() {
    boot();
    const size_t m = outMark();
    Serial.feed("{\"cmd\":\"arm\"");             // Pi died mid-write
    Serial.feed("{\"cmd\":\"disarm\"}\n");       // its restart's first line
    step(1);
    CHECK(!gLatch.armed);
    CHECK(countLinesWith("\"armed\":true", m) == 0);
    // A fragment with no '{' does nothing either.
    send("\"cmd\":\"arm\"}");
    step(1);
    CHECK(!gLatch.armed);
    // And the next clean line works.
    send(kArm);
    step(1);
    CHECK(gLatch.armed);
}

// Accepted by the owner 2026-10-07: estop disarms; estop_clear does not re-arm.
static void test_estop_disarms() {
    boot();
    send(kArm);
    CHECK(runWith(1000, kDrive05, 33));
    send("{\"cmd\":\"estop\"}");
    step(1);
    CHECK(allMotorsOff());
    CHECK(!gLatch.armed);
    send("{\"cmd\":\"estop_clear\"}");
    CHECK(!runWith(1000, kDrive05, 33));
    CHECK(gLatch.trips == 0);
    send(kArm);
    CHECK(runWith(1000, kDrive05, 33));
}

// Re-review notes: millis() rollover through the real loop(), and disarm
// while driving cuts every output.
static void test_rollover_and_disarm() {
    boot(0xFFFFFFFFu - 1500u);                       // ~1.5 s before the 49.7-day wrap
    send(kArm);
    CHECK(runWith(3000, kDrive05, 33));              // drives straight across the wrap
    CHECK(fake::g_millis < 5000u);                   // the clock really wrapped
    CHECK(gLatch.armed);
    CHECK(gLatch.trips == 0);
    CHECK(anyMotorOn());
    const uint32_t lastFeed = gLatch.lastFeedMs;
    while ((uint32_t)(fake::g_millis - lastFeed) < 500u) step(1);
    CHECK(anyMotorOn());                             // +500: not yet
    step(1);
    CHECK((uint32_t)(fake::g_millis - lastFeed) == 501u);
    CHECK(!gLatch.armed && allMotorsOff());          // +501: cut
    // A trip whose silence itself spans the wrap.
    boot(0xFFFFFFFFu - 300u);
    send(kArm);
    runWith(100, kDrive05, 33);
    const uint32_t lf = gLatch.lastFeedMs;
    while (gLatch.armed && (uint32_t)(fake::g_millis - lf) < 2000u) step(1);
    CHECK((uint32_t)(fake::g_millis - lf) == 501u);
    CHECK(allMotorsOff());

    boot();
    send(kArm);
    CHECK(runWith(1000, kDrive05, 33));
    const size_t m = outMark();
    send("{\"cmd\":\"disarm\"}");
    step(1);
    CHECK(!gLatch.armed);
    CHECK(allMotorsOff());
    CHECK(countLinesWith("\"armed\":false", m) == 1);
    CHECK(!runWith(1000, kDrive05, 33));             // stays off until arm
}

int main() {
    test_boot_defaults();
    test_turn_ceiling();
    test_trip_hard_stop_and_latch();
    test_trip_ignores_ramp();
    test_arm_zeroes_stale_target();
    test_non_operator_frames_do_not_feed();
    test_clock_race_at_loop_level();
    test_pin_set_never_allow();
    test_numeric_strictness();
    test_concatenated_partial_line();
    test_estop_disarms();
    test_rollover_and_disarm();
    std::printf("firmware_sim_test: %d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
