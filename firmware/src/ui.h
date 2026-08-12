#pragma once
#include "data.h"
#include "ble.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    // Extra pages, built only on boards that report rich_info (wide landscape
    // panels with room to spare). ui_show_screen() falls back to SCREEN_USAGE
    // for these on boards where they were never built, so the navigation cycle
    // collapses to splash<->usage exactly as before.
    SCREEN_LIMITS,   // per-window deep dive: burn rate, projection, overage
    SCREEN_SYSTEM,   // device, link and daemon diagnostics
    SCREEN_ACTIVITY, // 7d x 24h heatmap from local session transcripts
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);
