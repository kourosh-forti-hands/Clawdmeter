#include "history_store.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>

// ---- Flash-wear budget --------------------------------------------------
//
// The ESP32's NVS partition lives in SPI NOR flash rated for roughly 100,000
// erase cycles per sector. The daemon posts a fresh sample about once a
// minute (see CLAUDE.md: POLL_INTERVAL=60), so flushing to NVS on every push
// would mean:
//
//   1440 writes/day  =>  100,000 / 1440 ≈ 69 days to exhaust the rating.
//
// That's unacceptable for a device meant to sit on a desk for years. Instead
// the ring lives in RAM (history_push only touches ring_session/ring_weekly)
// and is mirrored to NVS at most once per FLUSH_INTERVAL_MS, and only when
// something changed since the last flush (`dirty`). Worst case — a sample
// arrives at least once in every single interval, forever, so every interval
// produces exactly one flush:
//
//   flushes/day = (24 * 60 * 60 * 1000) / FLUSH_INTERVAL_MS = 144
//   lifetime    = 100,000 / 144 ≈ 694 days ≈ 1.9 years of continuous,
//                 always-connected operation before the *rated* cycle count
//                 is reached.
//
// Two reasons this is a conservative floor rather than a real estimate:
//   - NVS is log-structured: putBytes() appends a new entry into the current
//     page and only erases when that page fills and gets garbage-collected,
//     so one call is well under one physical erase cycle amortized, not 1:1.
//   - The device isn't powered and BLE-connected 24/7 — sleep, disconnects,
//     and dev-cycle reflashes all mean real days accrue far fewer than 144
//     flushes.
// 1.9 years worst-case is judged an acceptable floor for a hobbyist desk
// gadget; see the "opportunistic first flush" note below for the one
// deliberate exception to the interval gate.
#define FLUSH_INTERVAL_MS (10UL * 60UL * 1000UL)  // 10 minutes

// Preferences namespace convention matches brightness.cpp's "clawdmeter"
// (see idle.cpp / brightness.cpp). Distinct key so it can't collide with
// brightness's "brt_idx" or anything else sharing the namespace.
#define NVS_NAMESPACE "clawdmeter"
#define NVS_KEY       "hist"

// Sanity marker at the front of the persisted blob so a firmware upgrade
// that changes the layout, or genuinely corrupt flash, is detected instead
// of being misread as valid samples.
#define HISTORY_BLOB_MAGIC 0x48495354UL  // 'HIST'

struct HistoryBlobHeader {
    uint32_t magic;
    uint16_t count;     // number of valid samples that follow, oldest first
    uint16_t reserved;  // padding for future use; always written as 0
};

#define BLOB_MAX_LEN (sizeof(HistoryBlobHeader) + (size_t)HISTORY_MAX * 2)

// ---- RAM ring ------------------------------------------------------------
// True circular buffer (head + count) rather than a shifting array, so a
// full ring drops the oldest sample in O(1) instead of memmove'ing ~120
// bytes on every push.
static uint8_t  ring_session[HISTORY_MAX];
static uint8_t  ring_weekly[HISTORY_MAX];
static uint16_t ring_head  = 0;  // slot holding the oldest retained sample
static uint16_t ring_count = 0;  // valid samples, 0..HISTORY_MAX

static bool     dirty         = false;  // pushed since the last flush?
static bool     ever_flushed  = false;  // has this boot written NVS yet?
static uint32_t last_flush_ms = 0;

static inline uint16_t ring_slot(uint16_t logical_i) {
    return (uint16_t)((ring_head + logical_i) % HISTORY_MAX);
}

uint16_t history_count(void) {
    return ring_count;
}

bool history_get(uint16_t i, uint8_t* session_pct, uint8_t* weekly_pct) {
    if (i >= ring_count) return false;
    uint16_t slot = ring_slot(i);
    if (session_pct) *session_pct = ring_session[slot];
    if (weekly_pct)  *weekly_pct  = ring_weekly[slot];
    return true;
}

static void flush_to_nvs(void) {
    HistoryBlobHeader hdr;
    hdr.magic    = HISTORY_BLOB_MAGIC;
    hdr.count    = ring_count;
    hdr.reserved = 0;

    uint8_t buf[BLOB_MAX_LEN];
    memcpy(buf, &hdr, sizeof(hdr));
    for (uint16_t i = 0; i < ring_count; i++) {
        // Reuse the public reader so the on-flash layout and the in-RAM
        // logical order can never drift apart.
        history_get(i, &buf[sizeof(hdr) + i * 2], &buf[sizeof(hdr) + i * 2 + 1]);
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;  // can't open r/w -> skip, try again next flush window
    prefs.putBytes(NVS_KEY, buf, sizeof(hdr) + (size_t)ring_count * 2);
    prefs.end();
}

// Called after every push. Rate-limited to FLUSH_INTERVAL_MS apart, with one
// deliberate exception: the very first sample this boot flushes immediately
// (`!ever_flushed`) so a device that reboots minutes after first pairing —
// or right after a reset — doesn't lose its only data point. That costs at
// most one extra write per boot, negligible against the budget above.
static void maybe_flush(void) {
    if (!dirty) return;

    uint32_t now = millis();
    if (ever_flushed && (now - last_flush_ms) < FLUSH_INTERVAL_MS) return;

    flush_to_nvs();
    dirty         = false;
    ever_flushed  = true;
    last_flush_ms = now;
}

void history_push(uint8_t session_pct, uint8_t weekly_pct) {
    uint16_t slot;
    if (ring_count < HISTORY_MAX) {
        slot = ring_slot(ring_count);
        ring_count++;
    } else {
        // Full: the write slot is the current oldest, which becomes this
        // new sample; advance head so the *next* oldest is one later.
        slot = ring_head;
        ring_head = (uint16_t)((ring_head + 1) % HISTORY_MAX);
    }
    ring_session[slot] = session_pct;
    ring_weekly[slot]  = weekly_pct;

    dirty = true;
    maybe_flush();
}

void history_init(void) {
    ring_head     = 0;
    ring_count    = 0;
    dirty         = false;
    ever_flushed  = false;
    last_flush_ms = millis();

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return;  // no NVS partition / can't open -> stay empty

    size_t stored_len = prefs.getBytesLength(NVS_KEY);
    if (stored_len < sizeof(HistoryBlobHeader) || stored_len > BLOB_MAX_LEN) {
        // Nothing persisted yet, or an implausible size -> stay empty rather
        // than risk reading out of bounds.
        prefs.end();
        return;
    }

    uint8_t buf[BLOB_MAX_LEN];
    size_t read_len = prefs.getBytes(NVS_KEY, buf, sizeof(buf));
    prefs.end();

    if (read_len != stored_len) return;  // short/failed read -> stay empty

    HistoryBlobHeader hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    size_t expected_len = sizeof(HistoryBlobHeader) + (size_t)hdr.count * 2;
    if (hdr.magic != HISTORY_BLOB_MAGIC || hdr.count > HISTORY_MAX || expected_len != stored_len) {
        // Corrupt blob, wrong version, or a count that doesn't match the
        // blob's own length -> stay empty rather than trust a partial read.
        return;
    }

    for (uint16_t i = 0; i < hdr.count; i++) {
        ring_session[i] = buf[sizeof(hdr) + i * 2];
        ring_weekly[i]  = buf[sizeof(hdr) + i * 2 + 1];
    }
    ring_count = hdr.count;  // ring_head stays 0: freshly loaded, not yet wrapped
}
