#pragma once

// Tracks short-term rate of change in session_pct (%/min) so the UI can react
// to *how heavily* Claude is being used right now, not just the current bucket
// level. Returns one of 4 group indices for the splash to pick animations from.

// Feed in the latest session percentage every time fresh BLE data arrives.
// Returns true when this sample is a session reset (pct dropped substantially
// vs the previous sample) — the caller uses this to chime the buzzer. Never
// true on the first sample after boot (no prior sample to compare against).
bool usage_rate_sample(float session_pct);

// 0 = idle, 1 = normal, 2 = active, 3 = heavy.
// Defaults to 0 when the buffer doesn't have enough samples yet.
int usage_rate_group(void);

// Current burn rate in percent per HOUR, derived from the same ring buffer
// as usage_rate_group() (oldest vs newest sample, same MIN_WINDOW_MS gate).
// Unlike the group logic this is signed — a declining session shows a
// negative rate rather than being clamped to zero, so usage_rate_mins_to_full()
// below can tell "flat/declining" apart from "not enough data yet".
// Returns -1.0f when there isn't yet MIN_WINDOW_MS of history to trust.
float usage_rate_pct_per_hour(void);

// Minutes until session usage would reach 100% at the current rate, based on
// the newest ring sample's percentage. Returns -1 when there isn't enough
// history to compute a rate yet, and -2 when the rate is zero or negative
// (usage is flat or falling, so it will never reach 100% by extrapolation) —
// the caller renders those two cases differently (e.g. "warming up" vs
// "steady").
int usage_rate_mins_to_full(void);
