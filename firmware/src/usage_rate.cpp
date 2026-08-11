#include "usage_rate.h"
#include <Arduino.h>

// Thresholds in %/min. A 5-hour (300 min) session ÷ 100% = 0.33 %/min to fill
// exactly at the same pace as the session itself resets — the user wants the
// "heavy" tier to start right there (filling in 4–5 hours).
//   < 0.10  →  Idle    (17h+ to fill, basically dormant)
//   < 0.20  →  Normal  (8–17h to fill, slow steady use)
//   < 0.33  →  Active  (5–8h, heavy but not yet pace-matching)
//   >=0.33  →  Heavy   (≤5h, matching or beating the session reset)
#define RATE_THRESH_NORMAL  0.10f
#define RATE_THRESH_ACTIVE  0.20f
#define RATE_THRESH_HEAVY   0.33f

// Minimum span between oldest and newest sample before we trust the computed
// rate. The whole point of the ring buffer is to smooth out single-sample
// jitter — at 60s daemon polling, a 1% bump between two consecutive samples
// looks like 1 %/min (Heavy) but really just means you grew 1% in the last
// minute. We require ~4 min of accumulated history so the rate reflects a
// real trend, not one noisy delta. Side-effect: ~4 min warm-up after boot
// during which we report Idle.
#define MIN_WINDOW_MS       240000UL

#define RING_SIZE 6

struct Sample { uint32_t ms; float pct; };

static Sample ring[RING_SIZE];
static uint8_t count = 0;
static uint8_t head  = 0;  // index of next write slot

static inline uint8_t oldest_idx(void) {
    return (head + RING_SIZE - count) % RING_SIZE;
}

static void usage_rate_reset(void) {
    count = 0;
    head  = 0;
}

bool usage_rate_sample(float session_pct) {
    uint32_t now = millis();
    bool was_reset = false;

    if (count > 0) {
        uint8_t latest = (head + RING_SIZE - 1) % RING_SIZE;
        // Session reset: pct dropped substantially. Restart tracking.
        if (session_pct + 5.0f < ring[latest].pct) {
            usage_rate_reset();
            was_reset = true;
        }
    }

    ring[head] = { now, session_pct };
    head = (head + 1) % RING_SIZE;
    if (count < RING_SIZE) count++;

    return was_reset;
}

int usage_rate_group(void) {
    if (count < 2) return 0;

    uint8_t o = oldest_idx();
    uint8_t l = (head + RING_SIZE - 1) % RING_SIZE;
    uint32_t dt = ring[l].ms - ring[o].ms;
    if (dt < MIN_WINDOW_MS) return 0;

    float dp = ring[l].pct - ring[o].pct;
    if (dp < 0.0f) dp = 0.0f;
    float rate = dp * 60000.0f / (float)dt;

    if (rate < RATE_THRESH_NORMAL) return 0;
    if (rate < RATE_THRESH_ACTIVE) return 1;
    if (rate < RATE_THRESH_HEAVY)  return 2;
    return 3;
}

// Shared by the two projection functions below. Same "do we trust this yet"
// gate as usage_rate_group() (count >= 2, span >= MIN_WINDOW_MS), but returns
// the *signed* %/min rate rather than clamping negative deltas to zero —
// group() clamps because it only ever buckets "how busy", never needs to
// distinguish a shrinking session from a flat one. Deliberately left
// out-of-line from usage_rate_group() so that function's behaviour stays
// byte-for-byte what it was before this file grew a second consumer of the
// ring buffer.
static bool usage_rate_compute(float *out_rate_pct_per_min, float *out_latest_pct) {
    if (count < 2) return false;

    uint8_t o = oldest_idx();
    uint8_t l = (head + RING_SIZE - 1) % RING_SIZE;
    uint32_t dt = ring[l].ms - ring[o].ms;
    if (dt < MIN_WINDOW_MS) return false;

    float dp = ring[l].pct - ring[o].pct;
    *out_rate_pct_per_min = dp * 60000.0f / (float)dt;
    *out_latest_pct = ring[l].pct;
    return true;
}

float usage_rate_pct_per_hour(void) {
    float rate_per_min, latest_pct;
    if (!usage_rate_compute(&rate_per_min, &latest_pct)) return -1.0f;
    return rate_per_min * 60.0f;
}

int usage_rate_mins_to_full(void) {
    float rate_per_min, latest_pct;
    if (!usage_rate_compute(&rate_per_min, &latest_pct)) return -1;
    if (rate_per_min <= 0.0f) return -2;

    float remaining = 100.0f - latest_pct;
    // Already at/over the cap by the newest sample — no wait left, rather
    // than a bogus negative minute count.
    if (remaining <= 0.0f) return 0;

    return (int)(remaining / rate_per_min);
}
