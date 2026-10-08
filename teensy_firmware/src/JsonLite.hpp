#pragma once
// ============================================================
//  JsonLite — the firmware's flat-JSON token helpers (no heap).
//
//  Moved out of main.cpp so the host tests (test/host/) exercise
//  the SAME matcher the firmware runs. No Arduino.h: this compiles
//  on a PC too.
//
//  Matching is by substring. Every key is passed WITH its quotes
//  ("\"drive\"") so "drive" cannot match inside "drive_max_fwd".
//
//  Numbers are strict (review finding 5, 2026-10-07): a value with
//  no digits ("x", null, a quoted "500") or a non-finite float
//  (nan, inf) is REJECTED. jsonTryGet* return false and leave *out
//  untouched, so a config key keeps its old value instead of
//  silently becoming 0. jsonGetFloat (drive inputs) returns 0, i.e.
//  neutral, for the same values.
// ============================================================
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

inline bool jsonTryGetFloat(const char* json, const char* key, float* out) {
    const char* p = strstr(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    char* end = nullptr;
    const float v = strtof(p + 1, &end);
    if (end == p + 1) return false;                     // no number at all
    if (!(v >= -FLT_MAX && v <= FLT_MAX)) return false; // nan, inf, overflow
    *out = v;
    return true;
}

inline float jsonGetFloat(const char* json, const char* key) {
    float v = 0.f;
    return jsonTryGetFloat(json, key, &v) ? v : 0.f;
}

inline bool jsonHasKey(const char* json, const char* key) {
    return strstr(json, key) != nullptr;
}

inline bool jsonTryGetInt(const char* json, const char* key, int* out) {
    const char* p = strstr(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    char* end = nullptr;
    long v = strtol(p + 1, &end, 10);
    if (end == p + 1) return false;                     // no digits
    if (v > INT_MAX) v = INT_MAX;                       // same saturation on
    if (v < INT_MIN) v = INT_MIN;                       // 32- and 64-bit long
    *out = (int)v;
    return true;
}
