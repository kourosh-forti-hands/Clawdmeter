// Host unit test for history_store.{h,cpp} -- the persistent ring buffer
// behind the usage-trend chart.
//
//   g++ -std=c++17 -I . -I ../../src test_main.cpp -o t && ./t
//
// history_store.cpp's non-portable dependencies are millis() (Arduino) and
// the ESP32 Preferences/NVS API, neither of which exist on a bare host. The
// "-I ." picks up this directory's own Arduino.h and Preferences.h -- tiny
// shims providing a settable fake clock and an in-memory stand-in for NVS,
// so persistence and time-based rate-limiting can be driven deterministically
// instead of sleeping in real time or touching real flash. See those two
// files for why they have to live in a separate include directory rather
// than next to history_store.cpp: its `#include <Arduino.h>` / `#include
// <Preferences.h>` are angle-bracket includes, which only resolve through
// -I search paths, never through the including file's own directory.
//
// history_store.cpp is pulled in by #include (not compiled/linked
// separately) because its ring buffer and flush state live in file-static
// variables with no accessors -- the only way to drive it from a host binary
// is to become part of the same translation unit. That also means this file
// sees history_store.cpp's private macros (FLUSH_INTERVAL_MS, NVS_NAMESPACE,
// NVS_KEY, HISTORY_BLOB_MAGIC) and its HistoryBlobHeader struct directly --
// used below instead of duplicating magic numbers, same as
// test_usage_rate/test_main.cpp reaching into usage_rate.cpp's RING_SIZE.

#include "history_store.h"
#include "../../src/history_store.cpp"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

// Common per-test setup: factory-blank fake NVS, zeroed write counter, clock
// back to 0. Every test calls this first so tests can run in any order.
static void reset_all() {
    Preferences::wipe_storage();
    Preferences::write_count() = 0;
    set_fake_millis(0);
}

static void should_be_empty_before_any_push() {
    reset_all();
    history_init();

    CHECK(history_count() == 0);
    uint8_t s = 0xAA, w = 0xAA;
    CHECK(!history_get(0, &s, &w));
    // Untouched by a failed read -- history_get must not write through on miss.
    CHECK(s == 0xAA);
    CHECK(w == 0xAA);
}

static void should_round_trip_push_and_read_oldest_first() {
    reset_all();
    history_init();

    history_push(10, 20);
    history_push(11, 21);
    history_push(12, 22);

    CHECK(history_count() == 3);

    uint8_t s, w;
    CHECK(history_get(0, &s, &w) && s == 10 && w == 20);  // oldest
    CHECK(history_get(1, &s, &w) && s == 11 && w == 21);
    CHECK(history_get(2, &s, &w) && s == 12 && w == 22);  // newest
    CHECK(!history_get(3, &s, &w));                        // one past the end
}

static void should_drop_oldest_when_ring_wraps_past_max() {
    reset_all();
    history_init();

    const int total_pushes = HISTORY_MAX + 5;
    for (int i = 0; i < total_pushes; i++) {
        // session/weekly kept distinguishable and derivable from i so the
        // post-wrap contents can be verified precisely, not just counted.
        history_push((uint8_t)(i % 250), (uint8_t)((i + 1) % 250));
    }

    CHECK(history_count() == HISTORY_MAX);

    // The first 5 pushes (i = 0..4) must have fallen off the front.
    int oldest_i = total_pushes - HISTORY_MAX;  // == 5
    uint8_t s, w;
    CHECK(history_get(0, &s, &w) && s == (uint8_t)(oldest_i % 250));
    CHECK(history_get(HISTORY_MAX - 1, &s, &w) &&
          s == (uint8_t)((total_pushes - 1) % 250));  // newest == last pushed
    CHECK(!history_get(HISTORY_MAX, &s, &w));
}

static void should_rate_limit_flushes_across_many_rapid_pushes() {
    reset_all();
    history_init();

    // 50 pushes without advancing the clock at all. Expected: the very
    // first push flushes immediately (the deliberate "get sample #1 onto
    // flash fast" exception), then every later push in the same instant is
    // rate-limited -- one flush total, not 50.
    const int rapid_pushes = 50;
    for (int i = 0; i < rapid_pushes; i++) {
        history_push((uint8_t)(i % 100), (uint8_t)(i % 100));
    }
    CHECK(Preferences::write_count() == 1);
    CHECK(Preferences::write_count() < (size_t)rapid_pushes);  // the core wear-limiting claim

    // Advancing past FLUSH_INTERVAL_MS re-arms the gate: the next push
    // should produce exactly one more flush.
    advance_fake_millis(FLUSH_INTERVAL_MS);
    history_push(42, 42);
    CHECK(Preferences::write_count() == 2);

    // Immediately after that flush, further rapid pushes are gated again.
    for (int i = 0; i < rapid_pushes; i++) {
        history_push((uint8_t)i, (uint8_t)i);
    }
    CHECK(Preferences::write_count() == 2);
}

static void should_restore_samples_after_simulated_reboot() {
    reset_all();
    history_init();

    const uint8_t sessions[5] = {5, 15, 25, 35, 45};
    const uint8_t weeklies[5] = {50, 51, 52, 53, 54};
    for (int i = 0; i < 5; i++) {
        history_push(sessions[i], weeklies[i]);
        // Space pushes >= FLUSH_INTERVAL_MS apart so each one flushes for
        // real (first via the "first sample" exception, the rest because
        // the interval has elapsed) -- guarantees NVS is fully caught up
        // with RAM by the time we "reboot" below, rather than testing a
        // reload that happens to only see a stale partial ring.
        advance_fake_millis(FLUSH_INTERVAL_MS);
    }
    CHECK(Preferences::write_count() == 5);

    // Simulate a reboot: re-run history_init() WITHOUT wiping the fake NVS
    // storage (RAM resets on a real reboot; flash content does not).
    history_init();

    CHECK(history_count() == 5);
    uint8_t s, w;
    for (int i = 0; i < 5; i++) {
        CHECK(history_get((uint16_t)i, &s, &w) && s == sessions[i] && w == weeklies[i]);
    }
}

static void should_start_empty_on_corrupt_blob() {
    reset_all();

    // Write garbage directly under the same namespace/key history_store
    // uses, bypassing history_push entirely -- simulates flash corruption or
    // a leftover blob from an incompatible firmware version rather than
    // anything history_store.cpp itself wrote.
    uint8_t garbage[7] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03};
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBytes(NVS_KEY, garbage, sizeof(garbage));
    prefs.end();

    history_init();

    CHECK(history_count() == 0);
    uint8_t s, w;
    CHECK(!history_get(0, &s, &w));
}

int main() {
    should_be_empty_before_any_push();
    should_round_trip_push_and_read_oldest_first();
    should_drop_oldest_when_ring_wraps_past_max();
    should_rate_limit_flushes_across_many_rapid_pushes();
    should_restore_samples_after_simulated_reboot();
    should_start_empty_on_corrupt_blob();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all history store tests passed\n");
    return 0;
}
