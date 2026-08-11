#pragma once
// Persistent ring buffer backing the usage-trend chart (ui.cpp's `hist_chart`,
// HIST_POINTS == HISTORY_MAX below). The chart itself only knows "shift a new
// point in"; this module is what lets the trend survive a reboot instead of
// starting empty every time, by mirroring the same samples into NVS.
//
// This module stores nothing but 0..100 percentages sampled roughly once a
// minute. It knows nothing about quota windows, plans, or reset cadences —
// no hardcoded account/plan assumptions live here (see CLAUDE.md's
// "no hardcoded config values" rule). See history_store.cpp for the flush
// cadence / flash-wear budget.

#include <stdint.h>

// Max samples retained. Sized so the whole ring fits comfortably in one NVS
// blob; the caller renders at most this many points.
#define HISTORY_MAX 120

// Load any persisted ring from NVS. Safe to call once at startup, before or
// after LVGL init. Never fails destructively — a missing or corrupt blob
// simply yields an empty history.
void history_init(void);

// Record one sample (percentages 0..100). Called once per daemon payload,
// roughly every 60 seconds. Persistence is internally rate-limited (see
// below) so the caller does not have to think about flash wear.
void history_push(uint8_t session_pct, uint8_t weekly_pct);

// Number of samples currently held, 0..HISTORY_MAX.
uint16_t history_count(void);

// Read sample i, OLDEST first (i = 0 is the oldest retained sample).
// Returns false when i >= history_count().
bool history_get(uint16_t i, uint8_t* session_pct, uint8_t* weekly_pct);
