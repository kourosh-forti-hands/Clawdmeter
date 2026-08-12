#pragma once
// Host-only stand-in for Arduino.h, used exclusively by this test. usage_rate.cpp's
// only Arduino dependency is millis() (uint32_t/uint8_t/float all come from the
// standard library); everything else in the real framework header is irrelevant
// here, so this shim provides just that one call, backed by a settable fake
// clock so tests can jump time forward deterministically instead of sleeping
// in real time.
//
// This file lives only under firmware/test/ -- PlatformIO's build_src_filter
// never looks there, so real board builds never see it, and usage_rate.cpp
// has no idea it's talking to a fake: it just calls millis() like always.
#include <stdint.h>

static uint32_t g_fake_millis = 0;

inline uint32_t millis(void) { return g_fake_millis; }

// Test-only hook (not part of the real Arduino API) for advancing the clock.
inline void set_fake_millis(uint32_t ms) { g_fake_millis = ms; }
