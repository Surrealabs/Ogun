#pragma once
// ============================================================
//  SafetyLogic — the Teensy fail-safe rules, free of Arduino.h
//  so they compile and run on a PC (test/host/).
//
//  Owner rulings 2026-10-07 (Surreallabs/reports/
//  ogun-offload-ticket-2026-10-07.md section 7):
//   Q1  Watchdog trip while armed = hard stop + DISARM, latched:
//       motion resumes only after an explicit {"cmd":"arm"}.
//       No coast, whatever ramp_sec says.
//   Q2  watchdog_ms defaults to 500; clamped to [100, 1000]
//       (1000 leaves room for a future remote path).
//   Q11 pin_set stays, gated by a runtime allowlist (default
//       EMPTY, set with fw_cfg "pin_allow"). Motor-driver pins
//       are refused ALWAYS: writing one bypasses the hard stop.
//
//  Only operator frames feed the watchdog: drive and hb, plus
//  arm, which opens a fresh window. sensor_req, stop, fw_cfg and
//  everything else do NOT, so a Pi that is alive but has lost
//  its operator can no longer hold the rover's last throttle.
// ============================================================
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "JsonLite.hpp"

namespace safety {

// ---- Watchdog window ---------------------------------------
constexpr uint32_t WATCHDOG_MIN_MS = 100;
constexpr uint32_t WATCHDOG_MAX_MS = 1000;

inline uint32_t clampWatchdogMs(long v) {
    if (v < (long)WATCHDOG_MIN_MS) return WATCHDOG_MIN_MS;
    if (v > (long)WATCHDOG_MAX_MS) return WATCHDOG_MAX_MS;
    return (uint32_t)v;
}

// Signed difference: a feed stamped one tick AFTER `now` was
// sampled reads as "just fed", not as a ~49-day gap (the unsigned
// underflow that would have been a false trip). Wraps correctly
// across the millis() rollover.
inline bool watchdogExpired(uint32_t now, uint32_t lastFeedMs, uint32_t windowMs) {
    const int32_t elapsed = (int32_t)(now - lastFeedMs);
    return elapsed > (int32_t)windowMs;
}

// ---- Command classification --------------------------------
enum class Cmd : uint8_t {
    None, Estop, EstopClear, Arm, Disarm, Stop, Drive, Hb,
    SensorReq, EncReset, Bootloader, PinDiag, PinSet, FwCfgGet, FwCfg
};

// Same order as the firmware's original if-chain, with hb slotted
// in after drive. Matching is by substring of the whole line, keys
// and values alike, so the first match wins: a line holding both
// "estop" and "drive" is an estop. The closing quote keeps
// "\"estop\"" from matching "\"estop_clear\"" and "\"fw_cfg\"" from
// matching "\"fw_cfg_get\"" (which is tested first anyway).
// "\"hb\"" only matches a JSON string that is exactly hb: no key or
// value the Pi sends today contains it (host test checks every one).
//
// A command line holds exactly ONE '{' (review finding 4). Every line
// the Pi sends is one flat object (pin_allow uses [ ]). Two '{' means
// a partial line ran into the next one: a Pi that died mid-write of
// {"cmd":"arm" followed by its restart's {"cmd":"disarm"} must not
// read as "arm". No '{' means a fragment. Either way: Cmd::None, which
// does nothing, not even feed the watchdog.
inline bool isSingleObject(const char* line) {
    const char* first = strchr(line, '{');
    return first != nullptr && strchr(first + 1, '{') == nullptr;
}

inline Cmd classifyCommand(const char* line) {
    if (!isSingleObject(line))               return Cmd::None;
    if (jsonHasKey(line, "\"estop\""))       return Cmd::Estop;
    if (jsonHasKey(line, "\"estop_clear\"")) return Cmd::EstopClear;
    if (jsonHasKey(line, "\"arm\""))         return Cmd::Arm;
    if (jsonHasKey(line, "\"disarm\""))      return Cmd::Disarm;
    if (jsonHasKey(line, "\"stop\""))        return Cmd::Stop;
    if (jsonHasKey(line, "\"drive\""))       return Cmd::Drive;
    if (jsonHasKey(line, "\"hb\""))          return Cmd::Hb;
    if (jsonHasKey(line, "\"sensor_req\""))  return Cmd::SensorReq;
    if (jsonHasKey(line, "\"enc_reset\""))   return Cmd::EncReset;
    if (jsonHasKey(line, "\"bootloader\""))  return Cmd::Bootloader;
    if (jsonHasKey(line, "\"pin_diag\""))    return Cmd::PinDiag;
    if (jsonHasKey(line, "\"pin_set\""))     return Cmd::PinSet;
    if (jsonHasKey(line, "\"fw_cfg_get\""))  return Cmd::FwCfgGet;
    if (jsonHasKey(line, "\"fw_cfg\""))      return Cmd::FwCfg;
    return Cmd::None;
}

// Operator frames only. arm counts: it opens the window for the
// session it starts (otherwise a stale stamp would trip at once).
inline bool feedsWatchdog(Cmd c) {
    return c == Cmd::Drive || c == Cmd::Hb || c == Cmd::Arm;
}

// ---- Arm state + watchdog latch ----------------------------
// The firmware's single source of truth for "armed". processCommand()
// calls onCommand() for every line; loop() calls poll() every pass,
// sampling `now` AFTER the serial input has been processed.
struct WatchdogLatch {
    bool     armed      = false;
    uint32_t lastFeedMs = 0;
    uint32_t trips      = 0;

    void onCommand(Cmd c, uint32_t now) {
        if (feedsWatchdog(c)) lastFeedMs = now;
        if (c == Cmd::Arm) armed = true;
        // estop disarms too: after estop_clear the operator must arm
        // again (the Pi already requires ignition_start), and the
        // watchdog does not log a spurious trip while the Pi answers
        // the latched estop with stop frames.
        if (c == Cmd::Disarm || c == Cmd::Estop) armed = false;
    }

    // True exactly once per trip. The caller must hard-stop the
    // motors (forceStop) and emit the safety line. Drive frames do
    // NOT re-arm; only Cmd::Arm does.
    bool poll(uint32_t now, uint32_t windowMs) {
        if (!armed || !watchdogExpired(now, lastFeedMs, windowMs)) return false;
        armed = false;
        trips++;
        return true;
    }
};

// ---- pin_set gate ------------------------------------------
// The range pin_set has always accepted (Teensy 4.1 edge pins).
constexpr int PIN_SET_MAX = 41;
static_assert(PIN_SET_MAX < 64, "allowlist is a 64-bit mask");

enum class PinVerdict : uint8_t { Ok, OutOfRange, MotorPin, NotAllowed };

inline const char* pinVerdictName(PinVerdict v) {
    switch (v) {
        case PinVerdict::Ok:         return "ok";
        case PinVerdict::OutOfRange: return "out_of_range";
        case PinVerdict::MotorPin:   return "motor_pin";
        case PinVerdict::NotAllowed: return "not_allowed";
    }
    return "unknown";
}

inline bool pinInMask(uint64_t mask, int pin) {
    return pin >= 0 && pin <= PIN_SET_MAX && ((mask >> pin) & 1ull) != 0;
}

// Motor pins are checked BEFORE the allowlist, so listing one in
// pin_allow changes nothing. Arming state is deliberately not an
// input (owner Q11).
inline PinVerdict pinSetVerdict(int pin, uint64_t allowMask,
                                const uint8_t* motorPins, size_t nMotorPins) {
    if (pin < 0 || pin > PIN_SET_MAX) return PinVerdict::OutOfRange;
    for (size_t i = 0; i < nMotorPins; ++i) {
        if ((int)motorPins[i] == pin) return PinVerdict::MotorPin;
    }
    if (!pinInMask(allowMask, pin)) return PinVerdict::NotAllowed;
    return PinVerdict::Ok;
}

// Parse  "key":[n, n, ...]  into a mask. All or nothing: returns
// false and leaves *out untouched if the key is absent, the value
// is not an array, or any entry is not a plain integer in
// 0..PIN_SET_MAX. "[]" is valid and clears the list.
inline bool jsonTryGetPinList(const char* json, const char* key, uint64_t* out) {
    const char* p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    auto skipWs = [&p]() { while (*p == ' ' || *p == '\t') ++p; };
    skipWs();
    if (*p != ':') return false;
    ++p;
    skipWs();
    if (*p != '[') return false;
    ++p;
    skipWs();
    uint64_t mask = 0;
    if (*p == ']') { *out = 0; return true; }
    for (;;) {
        skipWs();
        if (*p < '0' || *p > '9') return false;      // no sign, no junk
        char* end = nullptr;
        const long v = strtol(p, &end, 10);
        if (end == p || v < 0 || v > PIN_SET_MAX) return false;
        p = end;
        mask |= (1ull << v);
        skipWs();
        if (*p == ',') { ++p; continue; }
        if (*p == ']') break;
        return false;                                // "30.5", "30e1", unterminated
    }
    *out = mask;
    return true;
}

// Writes "[a,b,c]". Never overruns `len`; drops trailing entries
// if the buffer is too small (still well-formed).
inline void formatPinList(uint64_t mask, char* buf, size_t len) {
    if (!buf || len == 0) return;
    if (len < 3) { buf[0] = '\0'; return; }
    size_t n = 0;
    buf[n++] = '[';
    bool first = true;
    for (int pin = 0; pin <= PIN_SET_MAX; ++pin) {
        if (!((mask >> pin) & 1ull)) continue;
        const size_t room = len - n;                 // bytes left incl. NUL
        const int w = snprintf(buf + n, room, first ? "%d" : ",%d", pin);
        if (w < 0 || (size_t)w + 2 > room) break;    // keep room for "]" + NUL
        n += (size_t)w;
        first = false;
    }
    buf[n++] = ']';
    buf[n] = '\0';
}

// ---- Serial line assembly ----------------------------------
// An overlong line is dropped WHOLE. The old reader reset its index
// on overflow and kept going, so the tail of a long line ran as a
// separate command. Lines up to N-1 chars are unchanged.
template <size_t N>
struct LineReader {
    static_assert(N >= 2, "need room for one char + NUL");
    char   buf[N];
    size_t idx      = 0;
    bool   overflow = false;

    // Feed one byte. Returns the completed NUL-terminated line
    // (valid until the next push) or nullptr.
    const char* push(char c) {
        if (c == '\n' || c == '\r') {
            const bool complete = (idx > 0 && !overflow);
            if (complete) buf[idx] = '\0';
            idx = 0;
            overflow = false;
            return complete ? buf : nullptr;
        }
        if (overflow) return nullptr;                // inside a dropped line
        if (idx < N - 1) { buf[idx++] = c; return nullptr; }
        overflow = true;                             // drop it all, resync at newline
        return nullptr;
    }
};

} // namespace safety
