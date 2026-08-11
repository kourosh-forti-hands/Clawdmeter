#pragma once
// Host-only stand-in for Arduino.h, used exclusively by this test.
// history_store.cpp's only Arduino dependency (besides <Preferences.h>, see
// Preferences.h in this same directory) is millis(); everything else in the
// real framework header is irrelevant here. Backed by a settable fake clock
// so tests can jump time forward deterministically to exercise the flush
// rate-limit instead of sleeping in real time.
//
// This file lives only under firmware/test/ -- PlatformIO's build_src_filter
// never looks there, so real board builds never see it, and history_store.cpp
// has no idea it's talking to a fake: it just calls millis() like always.
#include <stdint.h>

static uint32_t g_fake_millis = 0;

inline uint32_t millis(void) { return g_fake_millis; }

// Test-only hook (not part of the real Arduino API) for advancing the clock.
inline void set_fake_millis(uint32_t ms) { g_fake_millis = ms; }
inline void advance_fake_millis(uint32_t delta_ms) { g_fake_millis += delta_ms; }
