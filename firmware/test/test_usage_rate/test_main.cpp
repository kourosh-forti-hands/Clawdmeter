// Host unit test for usage_rate.{h,cpp} -- the burn-rate / time-to-limit
// projection layered on top of the existing splash-group ring buffer.
//
//   g++ -std=c++17 -I ../../src -I . test_main.cpp -o t && ./t
//
// usage_rate.cpp's only non-portable dependency is millis(), which lives in
// the Arduino/ESP-IDF framework and doesn't exist on a bare host. The extra
// "-I ." picks up this directory's own Arduino.h -- a tiny shim providing
// just millis(), backed by a settable fake clock so time can be advanced
// deterministically instead of sleeping in real time. See Arduino.h for why
// it has to be a separate include directory rather than living next to
// usage_rate.cpp: that file's `#include <Arduino.h>` is an angle-bracket
// include, so it only resolves through -I search paths, never through the
// including file's own directory.
//
// usage_rate.cpp is pulled in by #include (not compiled/linked separately)
// because its ring buffer and rate math live in file-static state with no
// accessor -- the only way to drive it from a host binary is to become part
// of the same translation unit.

#include "usage_rate.h"
#include "../../src/usage_rate.cpp"
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static bool approx(float a, float b) { return std::fabs(a - b) < 0.01f; }

// Drives the ring back to a known-empty state via the public API only --
// there's no direct reset hook, and there shouldn't be one just for tests.
// Pushing a deliberately huge sample and then a near-zero one is guaranteed
// to trip usage_rate_sample()'s session-reset detection (a >5 drop) no
// matter what was in the ring before, leaving count == 1.
static void reset_ring() {
    set_fake_millis(0);
    usage_rate_sample(1000.0f);
    usage_rate_sample(0.0f);
}

// Pushes RING_SIZE fresh samples, evenly spaced by step_ms, rising from
// start_pct by step_pct per sample. RING_SIZE pushes right after
// reset_ring()'s single leftover sample is exactly one more than the ring
// holds, so the leftover always gets evicted -- oldest == the first sample
// pushed here, newest == the last, regardless of whatever ran before this
// call. That makes every test's window fully deterministic.
static void fill_ring(float start_pct, float step_pct, uint32_t step_ms) {
    for (int i = 0; i < RING_SIZE; i++) {
        set_fake_millis((uint32_t)i * step_ms);
        usage_rate_sample(start_pct + step_pct * (float)i);
    }
}

static void should_report_unknown_with_no_samples_yet() {
    // Relies on running first: file-static ring state is zero-initialized
    // at process start, same as a fresh boot before any BLE payload arrives.
    CHECK(usage_rate_pct_per_hour() == -1.0f);
    CHECK(usage_rate_mins_to_full() == -1);
    CHECK(usage_rate_group() == 0);
}

static void should_report_unknown_before_min_window_elapses() {
    reset_ring();
    fill_ring(10.0f, 1.0f, 10000);  // 6 samples, 10s apart -> 50s span, well under MIN_WINDOW_MS
    CHECK(usage_rate_pct_per_hour() == -1.0f);
    CHECK(usage_rate_mins_to_full() == -1);
    CHECK(usage_rate_group() == 0);
}

static void should_compute_pct_per_hour_and_mins_to_full_for_steady_climb() {
    reset_ring();
    // 10 -> 20 over 5 minutes = 2%/min = 120%/hour; 80% of headroom left at
    // that pace takes 40 minutes.
    fill_ring(10.0f, 2.0f, 60000);
    CHECK(approx(usage_rate_pct_per_hour(), 120.0f));
    CHECK(usage_rate_mins_to_full() == 40);
}

static void should_report_not_trending_for_flat_rate() {
    reset_ring();
    fill_ring(50.0f, 0.0f, 60000);  // no movement at all
    CHECK(usage_rate_pct_per_hour() == 0.0f);
    CHECK(usage_rate_mins_to_full() == -2);
}

static void should_report_not_trending_for_declining_rate() {
    reset_ring();
    fill_ring(20.0f, -1.0f, 60000);  // drifting down (not a reset -- steps are <5)
    CHECK(usage_rate_pct_per_hour() < 0.0f);
    CHECK(usage_rate_mins_to_full() == -2);
}

static void should_restart_tracking_on_session_reset() {
    reset_ring();
    fill_ring(10.0f, 2.0f, 60000);
    CHECK(usage_rate_pct_per_hour() > 0.0f);  // sanity: warmed up before the reset

    set_fake_millis(6 * 60000);
    bool was_reset = usage_rate_sample(2.0f);  // 20 -> 2 is a >5 drop
    CHECK(was_reset);
    CHECK(usage_rate_group() == 0);
    CHECK(usage_rate_pct_per_hour() == -1.0f);
    CHECK(usage_rate_mins_to_full() == -1);
}

static void should_preserve_group_buckets_for_representative_rates() {
    // Each case spans RING_SIZE samples over 5 real minutes (300000ms), so
    // rate = total_dp * 0.2 %/min. Picked to land just inside each of
    // usage_rate_group()'s original thresholds (0.10 / 0.20 / 0.33 %/min).
    reset_ring();
    fill_ring(10.0f, 0.05f, 60000);  // total dp 0.25 -> rate 0.05 %/min
    CHECK(usage_rate_group() == 0);  // idle

    reset_ring();
    fill_ring(10.0f, 0.15f, 60000);  // total dp 0.75 -> rate 0.15 %/min
    CHECK(usage_rate_group() == 1);  // normal

    reset_ring();
    fill_ring(10.0f, 0.25f, 60000);  // total dp 1.25 -> rate 0.25 %/min
    CHECK(usage_rate_group() == 2);  // active

    reset_ring();
    fill_ring(10.0f, 0.40f, 60000);  // total dp 2.00 -> rate 0.40 %/min
    CHECK(usage_rate_group() == 3);  // heavy
}

int main() {
    should_report_unknown_with_no_samples_yet();
    should_report_unknown_before_min_window_elapses();
    should_compute_pct_per_hour_and_mins_to_full_for_steady_climb();
    should_report_not_trending_for_flat_rate();
    should_report_not_trending_for_declining_rate();
    should_restart_tracking_on_session_reset();
    should_preserve_group_buckets_for_representative_rates();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all usage rate tests passed\n");
    return 0;
}
