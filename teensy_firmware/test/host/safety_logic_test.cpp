// ============================================================
//  Host test for teensy_firmware/src/SafetyLogic.hpp
//  (watchdog latch, watchdog_ms clamp, command classifier,
//  pin_set allowlist, serial line reader). No Arduino, no board.
//
//  Build and run from this directory with any C++17 compiler:
//    g++ -std=c++17 -Wall -Wextra -I ../../src safety_logic_test.cpp -o safety_logic_test && ./safety_logic_test
//    cl /nologo /std:c++17 /W4 /EHsc /I ..\..\src safety_logic_test.cpp && safety_logic_test.exe
//  Exit code 0 = all checks passed.
//
//  Not a PlatformIO test suite on purpose (folder is not test_*):
//  `pio run` never compiles it, and it needs no gcc on Windows.
// ============================================================
#include "SafetyLogic.hpp"

#include <cstdio>
#include <string>
#include <vector>

using safety::Cmd;

static int gPass = 0;
static int gFail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (cond) { ++gPass; }                                               \
        else { ++gFail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static const char* cmdName(Cmd c) {
    switch (c) {
        case Cmd::None: return "None";           case Cmd::Estop: return "Estop";
        case Cmd::EstopClear: return "EstopClear"; case Cmd::Arm: return "Arm";
        case Cmd::Disarm: return "Disarm";       case Cmd::Stop: return "Stop";
        case Cmd::Drive: return "Drive";         case Cmd::Hb: return "Hb";
        case Cmd::SensorReq: return "SensorReq"; case Cmd::EncReset: return "EncReset";
        case Cmd::Bootloader: return "Bootloader"; case Cmd::PinDiag: return "PinDiag";
        case Cmd::PinSet: return "PinSet";       case Cmd::FwCfgGet: return "FwCfgGet";
        case Cmd::FwCfg: return "FwCfg";
    }
    return "?";
}

// Every line shape the Pi sends the Teensy today, copied from
// pi_server/src/serial/TeensyBridge.cpp (sendDrive/sendStop/
// requestSensors) and pi_server/src/main.cpp (sendRaw call sites,
// teensyFwConfigCommand with rover.conf defaults, drive_tune fw_cfg).
static const char* kPiStartupFwCfg =
    "{\"cmd\":\"fw_cfg\",\"l_rpwm\":23,\"l_lpwm\":22,\"l_en\":20,\"r_rpwm\":19,\"r_lpwm\":18,"
    "\"r_en\":16,\"t_rpwm\":15,\"t_lpwm\":14,\"t_en\":41,\"enc_la\":2,\"enc_lb\":3,\"enc_ra\":4,"
    "\"enc_rb\":5,\"vbat_adc\":14,\"curr_l_adc\":21,\"curr_r_adc\":17,\"curr_t_adc\":13,"
    "\"temp_adc\":23,\"vbat_div\":4.03,\"curr_zero_mv\":0,\"curr_sens_mv_per_a\":8.5,"
    "\"max_pwm\":255,\"min_pwm\":0,\"ramp_sec\":1,\"invert_left\":0,\"invert_right\":0,"
    "\"turn_max_pwm\":90,\"invert_turn\":0,\"turn_slowdown\":0.85,\"turn_ramp_sec\":0.35,"
    "\"watchdog_ms\":500,\"telem_ms\":100,\"input_deadband\":0.05,\"require_arm\":1}";

struct LineCase { const char* line; Cmd expect; };
static const LineCase kPiLines[] = {
    { "{\"cmd\":\"drive\",\"l\":0.5,\"r\":0.5,\"t\":-0.3}", Cmd::Drive },
    { "{\"cmd\":\"drive\",\"l\":0,\"r\":0,\"t\":0}",        Cmd::Drive },
    { "{\"cmd\":\"stop\"}",                                 Cmd::Stop },
    { "{\"cmd\":\"sensor_req\"}",                           Cmd::SensorReq },
    { "{\"cmd\":\"arm\"}",                                  Cmd::Arm },
    { "{\"cmd\":\"disarm\"}",                               Cmd::Disarm },
    { "{\"cmd\":\"estop\"}",                                Cmd::Estop },
    { "{\"cmd\":\"estop_clear\"}",                          Cmd::EstopClear },
    { "{\"cmd\":\"bootloader\"}",                           Cmd::Bootloader },
    { "{\"cmd\":\"pin_diag\"}",                             Cmd::PinDiag },
    { "{\"cmd\":\"pin_set\",\"pin\":26,\"val\":0}",         Cmd::PinSet },
    { "{\"cmd\":\"fw_cfg\",\"max_pwm\":255,\"min_pwm\":0,\"ramp_sec\":1,\"invert_left\":0,"
      "\"invert_right\":0,\"turn_max_pwm\":90,\"invert_turn\":0,\"turn_slowdown\":0.85,"
      "\"turn_ramp_sec\":0.35}",                            Cmd::FwCfg },
    { kPiStartupFwCfg,                                      Cmd::FwCfg },
    // Firmware-side commands with no Pi sender today:
    { "{\"cmd\":\"enc_reset\"}",                            Cmd::EncReset },
    { "{\"cmd\":\"fw_cfg_get\"}",                           Cmd::FwCfgGet },
    { "{\"cmd\":\"fw_cfg\",\"pin_allow\":[30,31]}",         Cmd::FwCfg },
    { "{\"cmd\":\"fw_cfg\",\"watchdog_ms\":5000}",          Cmd::FwCfg },
    // New:
    { "{\"cmd\":\"hb\"}",                                   Cmd::Hb },
    { "{\"cmd\": \"hb\"}",                                  Cmd::Hb },
    { "{\"cmd\":\"hb\",\"seq\":17}",                        Cmd::Hb },
};

static void test_classify_and_hb_collisions() {
    for (const auto& c : kPiLines) {
        const Cmd got = safety::classifyCommand(c.line);
        if (got != c.expect) {
            std::printf("  classify mismatch: got %s want %s for %.60s\n",
                        cmdName(got), cmdName(c.expect), c.line);
        }
        CHECK(got == c.expect);
    }
    // Review finding 4: exactly one '{' per command line.
    CHECK(safety::classifyCommand("{\"cmd\":\"arm\"{\"cmd\":\"disarm\"}") == Cmd::None);  // Pi died mid-write
    CHECK(safety::classifyCommand("{\"cmd\":\"dis{\"cmd\":\"drive\",\"l\":1,\"r\":1,\"t\":0}") == Cmd::None);
    CHECK(safety::classifyCommand("\"cmd\":\"arm\"}") == Cmd::None);                   // fragment, no '{'
    CHECK(safety::classifyCommand("\"arm\"") == Cmd::None);
    CHECK(safety::classifyCommand("") == Cmd::None);
    CHECK(safety::classifyCommand("{\"cmd\":\"fw_cfg\",\"pin_allow\":[30,31]}") == Cmd::FwCfg);  // [] is fine
    // Words that contain "hb" but are not exactly hb never classify as Hb.
    CHECK(safety::classifyCommand("{\"cmd\":\"hbx\"}") == Cmd::None);
    CHECK(safety::classifyCommand("{\"cmd\":\"xhb\"}") == Cmd::None);
    CHECK(safety::classifyCommand("{\"cmd\":\"hb_\"}") == Cmd::None);
    CHECK(safety::classifyCommand("{\"cmd\":\"HB\"}")  == Cmd::None);
    // Every fw_cfg key the firmware reads, one per line: none is "hb".
    const char* keys[] = {
        "l_rpwm","l_lpwm","l_en","r_rpwm","r_lpwm","r_en","t_rpwm","t_lpwm","t_en",
        "enc_la","enc_lb","enc_ra","enc_rb","vbat_adc","curr_l_adc","curr_r_adc",
        "curr_t_adc","temp_adc","vbat_div","curr_zero_mv","curr_sens_mv_per_a",
        "watchdog_ms","telem_ms","drive_max_fwd","drive_max_rev","turn_max",
        "throttle_expo","turn_expo","accel_up_per_s","accel_down_per_s","max_pwm",
        "min_pwm","ramp_sec","invert_left","invert_right","turn_max_pwm","invert_turn",
        "turn_slowdown","turn_ramp_sec","low_volt_cutoff","low_volt_resume",
        "input_deadband","require_arm","pin_allow","pin","val","l","r","t" };
    for (const char* k : keys) {
        std::string line = std::string("{\"cmd\":\"fw_cfg\",\"") + k + "\":1}";
        CHECK(safety::classifyCommand(line.c_str()) == Cmd::FwCfg);
    }
    // And no Pi line other than the hb ones classifies as Hb.
    int hbCount = 0;
    for (const auto& c : kPiLines) hbCount += (safety::classifyCommand(c.line) == Cmd::Hb);
    CHECK(hbCount == 3);
}

static void test_feeds_watchdog() {
    CHECK(safety::feedsWatchdog(Cmd::Drive));
    CHECK(safety::feedsWatchdog(Cmd::Hb));
    CHECK(safety::feedsWatchdog(Cmd::Arm));
    // The bug: sensor_req fed it. Now nothing else does.
    const Cmd nonFeeders[] = { Cmd::SensorReq, Cmd::Stop, Cmd::None, Cmd::Estop,
        Cmd::EstopClear, Cmd::Disarm, Cmd::EncReset, Cmd::Bootloader, Cmd::PinDiag,
        Cmd::PinSet, Cmd::FwCfgGet, Cmd::FwCfg };
    for (Cmd c : nonFeeders) CHECK(!safety::feedsWatchdog(c));
}

static void test_watchdog_expired() {
    CHECK(!safety::watchdogExpired(1500, 1000, 500));   // exactly the window: not yet
    CHECK( safety::watchdogExpired(1501, 1000, 500));   // one ms past: trip
    CHECK(!safety::watchdogExpired(1000, 1001, 500));   // feed stamped after `now` (the race)
    CHECK(!safety::watchdogExpired(1000, 1005, 100));
    // millis() rollover: fed 100 ms before the wrap, now 300 ms after it.
    CHECK(!safety::watchdogExpired(250u, 0xFFFFFFFFu - 99u, 500));   // 350 ms elapsed
    CHECK( safety::watchdogExpired(450u, 0xFFFFFFFFu - 99u, 500));   // 550 ms elapsed
}

static void test_clamp() {
    CHECK(safety::clampWatchdogMs(-5) == 100);
    CHECK(safety::clampWatchdogMs(0) == 100);
    CHECK(safety::clampWatchdogMs(99) == 100);
    CHECK(safety::clampWatchdogMs(100) == 100);
    CHECK(safety::clampWatchdogMs(500) == 500);
    CHECK(safety::clampWatchdogMs(1000) == 1000);
    CHECK(safety::clampWatchdogMs(1001) == 1000);
    CHECK(safety::clampWatchdogMs(30000) == 1000);
    CHECK(safety::clampWatchdogMs(2147483647L) == 1000);
    // Through the real parse path main.cpp uses:
    int vi = 0;
    CHECK(jsonTryGetInt("{\"cmd\":\"fw_cfg\",\"watchdog_ms\":5000}", "\"watchdog_ms\"", &vi));
    CHECK(safety::clampWatchdogMs(vi) == 1000);
    CHECK(jsonTryGetInt("{\"cmd\":\"fw_cfg\",\"watchdog_ms\":-1}", "\"watchdog_ms\"", &vi));
    CHECK(safety::clampWatchdogMs(vi) == 100);
    // Review finding 5: a non-numeric value is REJECTED (old value kept), not read as 0.
    const char* bad[] = { "{\"watchdog_ms\":\"x\"}", "{\"watchdog_ms\":null}", "{\"watchdog_ms\":abc}",
                          "{\"watchdog_ms\":\"500\"}", "{\"watchdog_ms\":}", "{\"watchdog_ms\": }",
                          "{\"watchdog_ms\"}" };
    for (const char* b : bad) {
        int keep = 777;
        CHECK(!jsonTryGetInt(b, "\"watchdog_ms\"", &keep));
        CHECK(keep == 777);
    }
}

static void test_strict_numbers() {
    int vi = 0;
    CHECK(jsonTryGetInt("{\"v\": 500}", "\"v\"", &vi) && vi == 500);          // leading space ok
    CHECK(jsonTryGetInt("{\"v\":-7}", "\"v\"", &vi) && vi == -7);
    CHECK(jsonTryGetInt("{\"v\":99999999999999999999}", "\"v\"", &vi) && vi == 2147483647);   // saturates
    CHECK(jsonTryGetInt("{\"v\":-99999999999999999999}", "\"v\"", &vi) && vi == (-2147483647 - 1));
    float vf = 0.f;
    CHECK(jsonTryGetFloat("{\"v\":0.35}", "\"v\"", &vf) && vf > 0.349f && vf < 0.351f);
    const char* badF[] = { "{\"v\":nan}", "{\"v\":NaN}", "{\"v\":inf}", "{\"v\":-inf}",
                           "{\"v\":infinity}", "{\"v\":1e39}", "{\"v\":\"1\"}", "{\"v\":null}", "{\"v\":x}" };
    for (const char* b : badF) {
        float keep = 4.5f;
        CHECK(!jsonTryGetFloat(b, "\"v\"", &keep));
        CHECK(keep == 4.5f);
    }
    // Drive inputs: garbage reads as 0 (neutral), never nan/inf.
    CHECK(jsonGetFloat("{\"cmd\":\"drive\",\"l\":nan}", "\"l\"") == 0.f);
    CHECK(jsonGetFloat("{\"cmd\":\"drive\",\"l\":inf}", "\"l\"") == 0.f);
    CHECK(jsonGetFloat("{\"cmd\":\"drive\",\"l\":-0.5}", "\"l\"") == -0.5f);
}

// Drives the latch exactly as main.cpp does: every line goes through
// classifyCommand + onCommand; poll() runs every loop pass with `now`
// sampled after input. Time steps in 1 ms ticks.
struct Sim {
    safety::WatchdogLatch latch;
    uint32_t now = 0;
    uint32_t window = 500;
    std::vector<uint32_t> tripTimes;

    void line(const char* l) { latch.onCommand(safety::classifyCommand(l), now); }
    void tick() { ++now; if (latch.poll(now, window)) tripTimes.push_back(now); }
    // Advance `ms`, sending `l` every `everyMs` (nullptr = silence).
    void run(uint32_t ms, const char* l = nullptr, uint32_t everyMs = 0) {
        for (uint32_t i = 0; i < ms; ++i) {
            if (l && everyMs && (now % everyMs) == 0) line(l);
            tick();
        }
    }
};

static const char* kDrive  = "{\"cmd\":\"drive\",\"l\":0.3,\"r\":0.3,\"t\":0}";
static const char* kSensor = "{\"cmd\":\"sensor_req\"}";
static const char* kStop   = "{\"cmd\":\"stop\"}";
static const char* kHb     = "{\"cmd\":\"hb\"}";
static const char* kArm    = "{\"cmd\":\"arm\"}";

static void test_latch() {
    // Boot disarmed: silence never trips.
    {
        Sim s; s.now = 1000;
        s.run(5000);
        CHECK(s.tripTimes.empty());
        CHECK(!s.latch.armed);
    }
    // Driving at the page's 33 ms rate: no trip. Then the operator link
    // dies while the Pi keeps polling sensor_req at 10 Hz (the bug):
    // trip at lastDrive + window + 1, exactly once, and it latches.
    {
        Sim s; s.now = 1000;
        s.line(kArm);
        CHECK(s.latch.armed);
        s.run(10000, kDrive, 33);
        CHECK(s.tripTimes.empty());
        const uint32_t lastFeed = s.latch.lastFeedMs;
        s.run(3000, kSensor, 100);
        CHECK(s.tripTimes.size() == 1);
        if (!s.tripTimes.empty()) CHECK(s.tripTimes[0] == lastFeed + 501);
        CHECK(!s.latch.armed);
        CHECK(s.latch.trips == 1);
        // Stop frames do not feed or re-arm either.
        s.run(2000, kStop, 50);
        CHECK(!s.latch.armed);
        // Drive frames resume (link back): still disarmed, no new trip.
        s.run(2000, kDrive, 33);
        CHECK(!s.latch.armed);
        CHECK(s.latch.trips == 1);
        // Only arm re-arms; a fresh window, and the next silence trips again.
        s.line(kArm);
        CHECK(s.latch.armed);
        s.run(400);
        CHECK(s.latch.armed);
        s.run(200);
        CHECK(!s.latch.armed);
        CHECK(s.latch.trips == 2);
    }
    // hb alone keeps an idle armed rover armed.
    {
        Sim s; s.now = 1000;
        s.line(kArm);
        s.run(10000, kHb, 100);
        CHECK(s.latch.armed);
        CHECK(s.tripTimes.empty());
    }
    // A feed slower than the window trips.
    {
        Sim s; s.now = 1000;
        s.line(kArm);
        s.run(3000, kDrive, 600);
        CHECK(s.tripTimes.size() == 1);
    }
    // estop disarms; estop_clear does not re-arm.
    {
        Sim s; s.now = 1000;
        s.line(kArm);
        s.line("{\"cmd\":\"estop\"}");
        CHECK(!s.latch.armed);
        s.line("{\"cmd\":\"estop_clear\"}");
        CHECK(!s.latch.armed);
        s.run(2000, kStop, 33);          // Pi answers drive with stop while latched
        CHECK(s.latch.trips == 0);       // no spurious trip logged
    }
    // The race fixed in loop(): a feed stamped one tick after `now`.
    {
        safety::WatchdogLatch l;
        l.onCommand(Cmd::Arm, 1000);
        l.onCommand(Cmd::Drive, 1801);   // stamped by processCommand at 1801
        CHECK(!l.poll(1800, 500));       // poll's `now` sampled at 1800
        CHECK(l.armed);
    }
    // The clamp bounds the window a client can ask for.
    {
        Sim s; s.now = 1000; s.window = safety::clampWatchdogMs(30000);
        s.line(kArm);
        s.run(5000);
        CHECK(s.tripTimes.size() == 1);
        if (!s.tripTimes.empty()) CHECK(s.tripTimes[0] == 1000 + 1001);
    }
}

static void test_pin_list_parse() {
    uint64_t m = 0xDEAD;
    CHECK(!safety::jsonTryGetPinList("{\"cmd\":\"fw_cfg\"}", "\"pin_allow\"", &m));
    CHECK(m == 0xDEAD);                                          // absent: unchanged
    CHECK(safety::jsonTryGetPinList("{\"pin_allow\":[]}", "\"pin_allow\"", &m) && m == 0);
    CHECK(safety::jsonTryGetPinList("{\"pin_allow\":[30,31]}", "\"pin_allow\"", &m));
    CHECK(m == ((1ull << 30) | (1ull << 31)));
    CHECK(safety::jsonTryGetPinList("{\"pin_allow\" : [ 0 , 41 ]}", "\"pin_allow\"", &m));
    CHECK(m == ((1ull << 0) | (1ull << 41)));
    const char* bad[] = {
        "{\"pin_allow\":[30,]}", "{\"pin_allow\":[a]}", "{\"pin_allow\":[-1]}",
        "{\"pin_allow\":[42]}",  "{\"pin_allow\":[30.5]}", "{\"pin_allow\":30}",
        "{\"pin_allow\":[30",    "{\"pin_allow\":[99999999999999999999]}",
        "{\"pin_allow\":\"30\"}", "{\"pin_allow\":[,30]}", "{\"pin_allow\":[3e1]}" };
    for (const char* b : bad) {
        uint64_t keep = 0x55;
        CHECK(!safety::jsonTryGetPinList(b, "\"pin_allow\"", &keep));
        CHECK(keep == 0x55);                                      // all or nothing
    }
}

static void test_pin_verdict() {
    // The VERDICT LOGIC only, on a synthetic never-allow list (3, 5, 7, 9).
    // The firmware's real list (every BTS7960 RPWM/LPWM/EN/IS pin, runtime and
    // wired) is built by main.cpp's buildNeverAllowPins() and is tested end to
    // end in firmware_sim_test.cpp, which compiles main.cpp itself (finding 3).
    const uint8_t never[] = { 3, 5, 7, 9, 5 };
    const uint64_t all = ~0ull;
    using V = safety::PinVerdict;
    for (uint8_t p : never) CHECK(safety::pinSetVerdict(p, all, never, sizeof(never)) == V::MotorPin);
    CHECK(safety::pinSetVerdict(30, 0, never, sizeof(never)) == V::NotAllowed);   // default empty
    CHECK(safety::pinSetVerdict(30, 1ull << 30, never, sizeof(never)) == V::Ok);
    CHECK(safety::pinSetVerdict(31, 1ull << 30, never, sizeof(never)) == V::NotAllowed);
    CHECK(safety::pinSetVerdict(4, all, never, sizeof(never)) == V::Ok);            // neighbour of a refused pin
    CHECK(safety::pinSetVerdict(42, all, never, sizeof(never)) == V::OutOfRange);
    CHECK(safety::pinSetVerdict(-1, all, never, sizeof(never)) == V::OutOfRange);
    CHECK(safety::pinSetVerdict(5, all, never, 0) == V::Ok);                        // empty list refuses nothing
    // Through the real fw_cfg parse: listing a never-allow pin changes nothing.
    uint64_t m = 0;
    CHECK(safety::jsonTryGetPinList("{\"cmd\":\"fw_cfg\",\"pin_allow\":[30,5,9]}", "\"pin_allow\"", &m));
    CHECK(safety::pinSetVerdict(30, m, never, sizeof(never)) == V::Ok);
    CHECK(safety::pinSetVerdict(5, m, never, sizeof(never)) == V::MotorPin);
    CHECK(safety::pinSetVerdict(9, m, never, sizeof(never)) == V::MotorPin);
    CHECK(std::string(safety::pinVerdictName(V::MotorPin)) == "motor_pin");
    CHECK(std::string(safety::pinVerdictName(V::NotAllowed)) == "not_allowed");
}

static void test_format_pin_list() {
    char b[160];
    safety::formatPinList(0, b, sizeof(b));
    CHECK(std::string(b) == "[]");
    safety::formatPinList((1ull << 30) | (1ull << 31), b, sizeof(b));
    CHECK(std::string(b) == "[30,31]");
    const uint64_t every = (1ull << 42) - 1;                     // pins 0..41
    safety::formatPinList(every, b, sizeof(b));
    CHECK(std::string(b).size() == 117);
    uint64_t back = 0;
    std::string wrapped = std::string("{\"pin_allow\":") + b + "}";
    CHECK(safety::jsonTryGetPinList(wrapped.c_str(), "\"pin_allow\"", &back) && back == every);
    char small[8];
    safety::formatPinList(every, small, sizeof(small));          // truncates, stays well-formed
    CHECK(std::string(small) == "[0,1,2]");
}

static std::vector<std::string> feed(safety::LineReader<256>& r, const std::string& bytes) {
    std::vector<std::string> out;
    for (char c : bytes) { const char* l = r.push(c); if (l) out.emplace_back(l); }
    return out;
}

static void test_line_reader() {
    {
        safety::LineReader<256> r;
        auto out = feed(r, "{\"cmd\":\"arm\"}\r\n{\"cmd\":\"hb\"}\n\n");
        CHECK(out.size() == 2);
        if (out.size() == 2) { CHECK(out[0] == "{\"cmd\":\"arm\"}"); CHECK(out[1] == "{\"cmd\":\"hb\"}"); }
    }
    {
        safety::LineReader<256> r;
        auto out = feed(r, std::string(255, 'x') + "\n");          // the limit: kept
        CHECK(out.size() == 1 && out[0].size() == 255);
        out = feed(r, std::string(256, 'y') + "\n{\"cmd\":\"stop\"}\n");  // one over: dropped whole
        CHECK(out.size() == 1);
        if (out.size() == 1) CHECK(out[0] == "{\"cmd\":\"stop\"}");
    }
    {
        // The Pi's real 515-char startup fw_cfg: dropped whole (never a
        // fragment), and the next line arrives intact.
        CHECK(std::string(kPiStartupFwCfg).size() == 515);
        safety::LineReader<256> r;
        auto out = feed(r, std::string(kPiStartupFwCfg) + "\n{\"cmd\":\"disarm\"}\n");
        CHECK(out.size() == 1);
        if (out.size() == 1) CHECK(out[0] == "{\"cmd\":\"disarm\"}");
    }
    {
        // A tail that WOULD have run as a command under the old reader.
        safety::LineReader<256> r;
        auto out = feed(r, std::string(300, ' ') + "{\"cmd\":\"arm\"}\n");
        CHECK(out.empty());
        CHECK(!r.overflow && r.idx == 0);                        // resynced
    }
}

int main() {
    test_classify_and_hb_collisions();
    test_feeds_watchdog();
    test_watchdog_expired();
    test_clamp();
    test_strict_numbers();
    test_latch();
    test_pin_list_parse();
    test_pin_verdict();
    test_format_pin_list();
    test_line_reader();
    std::printf("safety_logic_test: %d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
