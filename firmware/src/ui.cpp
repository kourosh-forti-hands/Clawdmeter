#include "ui.h"
#include "splash.h"
#include "usage_rate.h"        // burn rate + projection for the Limits page
#include "history_store.h"     // trend samples that survive a reboot
#include <lvgl.h>
#include <time.h>
#include <Arduino.h>           // millis() for the System page's uptime
#include <esp_heap_caps.h>     // free heap / PSRAM for the System page
#include "logo.h"
#include "clawd_still.h"
#include "icons.h"
#include "hal/board_caps.h"
#include "usage_layout.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);
LV_FONT_DECLARE(font_mono_32);
LV_FONT_DECLARE(font_mono_18);

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions below.
struct Layout {
    int16_t scr_w, scr_h;
    int16_t margin;
    int16_t title_y;
    int16_t content_y;
    int16_t content_w;
    UsageSlots slots;                // panel placement (stacked or two-column)

    // Rich info mode — wide landscape panels have room for a pace/detail line
    // inside each panel plus a footer status strip. Off everywhere else, where
    // the extra text would crowd the layout.
    bool    rich_info;
    bool    use_arcs;                // radial gauges instead of bars
    int16_t detail_y;                // y offset INSIDE each usage panel
    int16_t footer_y;                // absolute y of the footer status strip
    int16_t arc_size;                // radial gauge diameter (rich_info only)
    int16_t text_x;                  // x of the text column beside the gauge
    const lv_font_t* detail_font;
    const lv_font_t* footer_font;

    // On-screen HID controls, for boards with no readable physical button
    bool    soft_buttons;
    int16_t softbtn_w, softbtn_h;
    int16_t softbtn_y;               // offset from the bottom edge (negative)
    int16_t softbtn_gap;
    const lv_font_t* softbtn_font;

    // Usage screen
    int16_t usage_panel_h;
    int16_t usage_panel_gap;
    int16_t usage_bar_y;
    int16_t usage_reset_y;
    int16_t bar_h;
    int16_t panel_pad_x, panel_pad_y;
    int16_t pill_pad_x, pill_pad_y;
    const lv_font_t* title_font;     // screen title / clock
    const lv_font_t* pct_font;       // big percentage number
    const lv_font_t* ent_pct_font;   // enterprise spending number
    const lv_font_t* pill_font;      // "Current" / "Weekly" pill
    const lv_font_t* reset_font;     // "Resets in ..." line
    const lv_font_t* pace_font;      // enterprise "Under/On/Over pace" line
    const lv_font_t* anim_font;      // animated status line
    int16_t anim_y;                  // status line offset from bottom
    bool    small_icons;             // 40px logo + 24px battery (vs 80/48) on small screens
    int16_t title_nudge;             // title x-shift balancing the corner logo
    int16_t logo_y;                  // logo top edge
    int16_t batt_y;                  // battery icon top edge
    int16_t batt_w;                  // battery icon width, for position math

    // Pairing hint / idle screen
    int16_t pair_y1, pair_y2, pair_y3;
    int16_t idle_px;                 // sleeping-creature size on the idle screen

    // Bluetooth screen
    int16_t bt_info_panel_h;
    int16_t bt_reset_zone_h;
    const lv_font_t* bt_title_font;
    const lv_font_t* bt_status_font;
    const lv_font_t* bt_device_font;
    const lv_font_t* bt_credit_1_font;
    const lv_font_t* bt_credit_2_font;
};
static Layout L = {};

// Pick layout values from the active board's pixel dimensions. The two
// existing boards happen to land on the two breakpoints below; new ports
// inherit the closer one — visually OK, may need a polish pass for
// pixel-perfect alignment but never blocks the port from booting.
static void compute_layout(const BoardCaps& c) {
    L.scr_w = c.width;
    L.scr_h = c.height;
    L.margin = 20;
    L.title_y = 30;

    // Values shared by the two original breakpoints; the small branch below
    // overrides them wholesale.
    L.bar_h = 24;
    L.panel_pad_x = 16;
    L.panel_pad_y = 12;
    L.pill_pad_x = 18;
    L.pill_pad_y = 6;
    L.title_font   = &font_tiempos_56;
    L.pct_font     = &font_styrene_48;
    L.ent_pct_font = &font_tiempos_56;
    L.pill_font    = &font_styrene_28;
    L.reset_font   = &font_styrene_28;
    L.pace_font    = &font_styrene_16;
    L.anim_font    = &font_mono_32;
    L.anim_y = -15;
    L.small_icons = false;
    L.title_nudge = 16;
    L.logo_y = L.title_y - 10;
    L.batt_y = L.title_y;
    L.batt_w = ICON_BATTERY_W;
    L.pair_y1 = 40;
    L.pair_y2 = 120;
    L.pair_y3 = 160;
    L.idle_px = 160;

    if (c.height >= 460) {
        // Large layout — tuned for 480x480 (AMOLED-2.16).
        L.content_y = 100;
        L.usage_panel_h = 150;
        L.usage_panel_gap = 16;
        L.usage_bar_y = 56;
        L.usage_reset_y = 94;
        L.bt_info_panel_h = 160;
        L.bt_reset_zone_h = 110;
        L.bt_title_font    = &font_tiempos_56;
        L.bt_status_font   = &font_styrene_48;
        L.bt_device_font   = &font_styrene_28;
        L.bt_credit_1_font = &font_styrene_24;
        L.bt_credit_2_font = &font_styrene_20;
    } else if (c.height >= 300) {
        // Compact layout — tuned for 368x448 (AMOLED-1.8).
        L.content_y = 85;
        L.usage_panel_h = 130;
        L.usage_panel_gap = 12;
        L.usage_bar_y = 48;
        L.usage_reset_y = 78;
        L.bt_info_panel_h = 140;
        L.bt_reset_zone_h = 90;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_28;
        L.bt_device_font   = &font_styrene_20;
        L.bt_credit_1_font = &font_styrene_16;
        L.bt_credit_2_font = &font_styrene_14;
    } else {
        // Small layout — tuned for 240x240 (LCD-1.54 and similar square TFTs).
        // Everything shrinks: fonts two steps down, panels ~half height, and
        // the corner logo/battery switch to the 40px/24px small assets.
        L.margin = 8;
        L.title_y = 4;
        L.content_y = 44;
        L.usage_panel_h = 74;
        L.usage_panel_gap = 6;
        L.usage_bar_y = 30;
        L.usage_reset_y = 46;
        L.bar_h = 12;
        L.panel_pad_x = 10;
        L.panel_pad_y = 6;
        L.pill_pad_x = 8;
        L.pill_pad_y = 2;
        L.title_font   = &font_tiempos_34;
        L.pct_font     = &font_styrene_24;
        L.ent_pct_font = &font_tiempos_34;
        L.pill_font    = &font_styrene_14;
        L.reset_font   = &font_styrene_14;
        L.pace_font    = &font_styrene_12;
        L.anim_font    = &font_mono_18;
        // Center the status line in the strip below the weekly panel; flush
        // against the bottom edge it reads as unevenly spaced.
        L.anim_y = -10;
        L.small_icons = true;
        L.title_nudge = 8;
        L.logo_y = 2;
        L.batt_y = 10;
        L.batt_w = ICON_BATTERY_SMALL_W;
        L.pair_y1 = 12;
        L.pair_y2 = 56;
        L.pair_y3 = 80;
        L.idle_px = 96;
        L.bt_info_panel_h = 90;
        L.bt_reset_zone_h = 60;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_20;
        L.bt_device_font   = &font_styrene_14;
        L.bt_credit_1_font = &font_styrene_12;
        L.bt_credit_2_font = &font_styrene_12;
    }

    L.content_w = L.scr_w - 2 * L.margin;

    // Wide landscape panels stack their content very differently: the two
    // usage panels sit side by side and only occupy one row, so the default
    // content_y tuned for portrait/square boards leaves a large dead band
    // between the panels and the controls below. Drop the row lower so the
    // vertical rhythm is title -> panels -> buttons -> status rather than
    // title -> panels -> void -> buttons.
    if (L.scr_w >= USAGE_TWO_COL_MIN_W && L.scr_w > L.scr_h) {
        L.content_y = 112;
        // Taller panels to fit the extra detail line, and the rich-info extras.
        L.usage_panel_h = 190;
        L.rich_info   = true;
        // Gauge centred in the panel with its text stacked underneath. Beside
        // the dial does not work at this panel width — "Under pace - 63% of 7d
        // gone" is wider than the 200 px that would be left over.
        // Canonical Clawdmeter layout (screenshots/usage.png), scaled to
        // 800x480: mascot top-left, clock centred, two stacked full-width
        // panels each with a big percentage, pill, bar and reset line, status
        // line beneath. The extra analysis lives one tap away rather than
        // crowding the screen you actually glance at.
        //
        // Vertical budget: clock 18..74, panels 84..214 and 230..360,
        // status ~370..388, keys 404..456.
        L.content_y       = 84;
        L.usage_panel_h   = 130;
        L.usage_panel_gap = 16;
        L.use_arcs        = false;     // official design uses bars
        // Panel internals compressed to fit 130 px (106 px of content):
        // pct 0..48, bar 52..72, reset 78..106.
        L.usage_bar_y     = 52;
        L.bar_h           = 20;
        L.usage_reset_y   = 78;
        L.anim_font       = &font_mono_18;
        L.anim_y          = -92;
        L.detail_font     = &font_styrene_20;
        L.footer_font     = &font_styrene_16;
        L.footer_y        = 0;
    }

    // Panel placement is derived, not per-breakpoint: every existing board
    // stacks, and wide landscape panels (the LCD-4.3) go side by side. Pinned
    // by test/test_usage_layout/.
    L.slots = usage_compute_slots(L.scr_w, L.scr_h, L.margin, L.content_y,
                                  L.usage_panel_h, L.usage_panel_gap);

    // Boards with no readable physical button get on-screen HID controls.
    // The LCD-4.3 is the first: GPIO 0 is an RGB data line there, so BOOT
    // cannot be read. Gated on the runtime capability, never on board name.
    L.soft_buttons = (c.button_count == 0);
    // Five keys across 800 px with 20 px margins: 5*142 + 4*12 = 758.
    L.softbtn_w    = L.rich_info ? 142 : 200;
    L.softbtn_h    = L.rich_info ? 60 : 72;
    L.softbtn_gap  = L.rich_info ? 12 : 24;
    // Raised clear of the status line, which sits at anim_y from the bottom.
    L.softbtn_y    = L.rich_info ? -22 : -68;
    L.softbtn_font = L.rich_info ? &font_styrene_24 : &font_styrene_28;
}

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// ---- Usage screen widgets (single non-splash view) ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
// Clock fed by the daemon: base epoch (local wall-clock seconds) + the lv_tick at
// which it landed, so the title ticks forward locally between 60s payloads.
static long     clock_base_epoch = 0;
static uint32_t clock_base_ms = 0;
static int      clock_fmt = 24;   // 12 or 24, set from the daemon payload
static int      clock_last_min = -1;   // last rendered minute; avoids redrawing the title every tick
static lv_obj_t* usage_group;   // the two usage panels — shown when connected
static lv_obj_t* pair_group;    // pairing hint — shown when disconnected
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
// Rich-info extras (wide landscape only; NULL elsewhere)
static lv_obj_t* lbl_session_detail = nullptr;
static lv_obj_t* lbl_weekly_detail  = nullptr;
static lv_obj_t* lbl_footer         = nullptr;
static lv_obj_t* lbl_corner_clock   = nullptr;

// ---- Extra pages (rich_info boards only; NULL elsewhere) ----
static lv_obj_t* arc_session = nullptr;   // radial gauges, rich_info only
static lv_obj_t* arc_weekly  = nullptr;
static lv_obj_t* limits_container = nullptr;
static lv_obj_t* system_container = nullptr;
// Two columns of key/value rows per page, refreshed in place so the pages cost
// no allocation after ui_init.
#define PAGE_ROWS 12
// First row / chart top on the extra pages. Clears the title (which runs to
// roughly y=86 at title_font) rather than reusing the usage page's content_y,
// which is tuned around the gauges and sits high enough to cut the descenders.
#define PAGE_TOP 100
static lv_obj_t* limits_rows[PAGE_ROWS] = {nullptr};
// History chart. LVGL's shift update mode owns the ring buffer, so there's no
// separate sample array to keep in step — one push per payload and the oldest
// point falls off the left edge. 120 points at the daemon's 60s cadence is
// about two hours of shape.
#define HIST_POINTS 120
static lv_obj_t*         hist_chart = nullptr;
static lv_chart_series_t* hist_session_ser = nullptr;
static lv_chart_series_t* hist_weekly_ser  = nullptr;
static lv_obj_t*          hist_hint = nullptr;   // "collecting" until a line exists
static uint16_t           hist_samples = 0;
static lv_obj_t* system_rows[PAGE_ROWS] = {nullptr};
// Whole last payload, kept so the Limits page can re-render on its own cadence
// without the caller having to push data at it.
static UsageData s_last_data = {};
// Cached from the last payload so ui_tick_anim can rebuild the footer string
// every second without keeping a pointer to the caller's UsageData. Freshness
// reuses the existing last_data_ms below, which update_view_state already
// maintains for its staleness check.
static bool      s_last_enterprise  = false;
static char      s_last_status[16]  = "";
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* panel_session = nullptr;
static lv_obj_t* panel_weekly = nullptr;
// Enterprise-only widgets inside panel_session
static lv_obj_t* lbl_session_pct_sym = nullptr;  // "%" in smaller font
static lv_obj_t* lbl_spending_desc = nullptr;     // "of your monthly budget"
static lv_obj_t* lbl_spending_status = nullptr;   // "Under pace" / "On pace" / "Over pace"
static lv_obj_t* lbl_anim;      // status line: connection state + whimsical idle

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* logo_img;
static lv_image_dsc_t battery_dscs[5];  // empty, low, medium, full, charging

// ---- Live-data freshness → which usage sub-view to show ----
// usage panels when data is flowing, an idle "Zzz" screen when the host is
// connected but no usage update landed within DATA_FRESH_MS, the pairing hint
// when BLE is down. Re-evaluated every loop in ui_tick_anim().
static lv_obj_t* idle_group;            // the "Zzz" idle screen
static uint32_t  last_data_ms = 0;      // lv_tick when the last valid usage update landed
static bool      data_received = false; // any valid update since boot
static bool      data_ok = true;        // last payload's ok flag; a {"ok":false} beat = "no fresh data"
static int       view_state = -1;       // -1 unknown / 0 pair / 1 idle / 2 usage
static const uint32_t DATA_FRESH_MS = 90000;  // usage counts as "live" within this window (daemon sends ~60s)

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;
static bool     s_ble_connected = false;   // cached BLE connection state
static uint32_t connected_at_ms = 0;       // when we last entered CONNECTED ("Connected" dwell)

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Rich-info detail line: how fast the budget is being spent relative to how
// much of the window has elapsed. A 5h window 60% gone with 4% used is very
// different from 4% used 5 minutes in, and the bare percentage can't say which.
// Colours match the enterprise pace verdict so the two views read alike.
static void format_pace_detail(float used_pct, int remaining_mins, int window_mins,
                               char* buf, size_t len) {
    if (remaining_mins < 0 || window_mins <= 0) {
        snprintf(buf, len, " ");   // window length unknown — say nothing
        return;
    }
    int elapsed = window_mins - remaining_mins;
    if (elapsed < 0)           elapsed = 0;
    if (elapsed > window_mins) elapsed = window_mins;
    const int elapsed_pct = (int)((long)elapsed * 100 / window_mins);

    const char* verdict;
    const char* hex;
    if (used_pct < (float)elapsed_pct - 10.0f)      { verdict = "Under pace"; hex = "788c5d"; }
    else if (used_pct > (float)elapsed_pct + 10.0f) { verdict = "Over pace";  hex = "c0392b"; }
    else                                            { verdict = "On pace";    hex = "d97757"; }

    // Window label derived from the length itself, so it always matches
    // whatever the API declared rather than a second hardcoded string.
    char wlabel[8];
    if (window_mins % 1440 == 0)   snprintf(wlabel, sizeof(wlabel), "%dd", window_mins / 1440);
    else if (window_mins % 60 == 0) snprintf(wlabel, sizeof(wlabel), "%dh", window_mins / 60);
    else                            snprintf(wlabel, sizeof(wlabel), "%dm", window_mins);

    // The verdict now sits in the text column beside the dial, which has room
    // for the elapsed figure it is derived from — so every board prints the
    // full sentence again.
    snprintf(buf, len, "#%s %s# - %d%% of %s gone", hex, verdict, elapsed_pct, wlabel);
}

// Gauge tint by pace rather than absolute level. Window length comes from the
// daemon (pulled from the API's header naming); when it's unknown we fall back
// to a neutral accent rather than implying a verdict we can't support.
static lv_color_t pace_color_for(float used_pct, int remaining_mins, int window_mins) {
    if (window_mins <= 0 || remaining_mins < 0) return COL_ACCENT;
    int elapsed = window_mins - remaining_mins;
    if (elapsed < 0)           elapsed = 0;
    if (elapsed > window_mins) elapsed = window_mins;
    const int elapsed_pct = (int)((long)elapsed * 100 / window_mins);
    if (used_pct < (float)elapsed_pct - 10.0f) return COL_GREEN;
    if (used_pct > (float)elapsed_pct + 10.0f) return COL_RED;
    return COL_AMBER;
}

// Forward decls — callbacks defined near ui_show_screen below
static void global_click_cb(lv_event_t* e);

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_right(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_top(panel, L.panel_pad_y, 0);
    lv_obj_set_style_pad_bottom(panel, L.panel_pad_y, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, L.pill_font, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_right(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_top(lbl, L.pill_pad_y, 0);
    lv_obj_set_style_pad_bottom(lbl, L.pill_pad_y, 0);
    return lbl;
}

static void init_battery_icons(void) {
    if (L.small_icons) {
        init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_SMALL_W, ICON_BATTERY_SMALL_H, icon_battery_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_SMALL_W, ICON_BATTERY_LOW_SMALL_H, icon_battery_low_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_SMALL_W, ICON_BATTERY_MEDIUM_SMALL_H, icon_battery_medium_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_SMALL_W, ICON_BATTERY_FULL_SMALL_H, icon_battery_full_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_SMALL_W, ICON_BATTERY_CHARGING_SMALL_H, icon_battery_charging_small_data);
        return;
    }
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}

// ======== Usage Screen ========

static lv_obj_t* make_usage_panel(lv_obj_t* parent, int x, int y, int w,
                                  const char* pill_text,
                                  lv_obj_t** out_pct, lv_obj_t** out_pill,
                                  lv_obj_t** out_bar, lv_obj_t** out_reset,
                                  lv_obj_t** out_arc) {
    lv_obj_t* panel = make_panel(parent, x, y, w, L.usage_panel_h);


    *out_pill = make_pill(panel, pill_text);
    // Caption heads the text column beside the dial on rich boards.
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    if (L.use_arcs) {
        // Radial gauge instead of a bar. On a wide panel a 340 px track showing
        // 5% is mostly empty pixels; a dial reads as a filled proportion at a
        // glance and puts the number where the eye already is — in the middle.
        // The bar is not created at all here, so every bar call site is guarded.
        lv_obj_t* arc = lv_arc_create(panel);
        lv_obj_set_size(arc, L.arc_size, L.arc_size);
        lv_obj_align(arc, LV_ALIGN_LEFT_MID, 0, 0);   // dial on the left
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 0);
        lv_arc_set_bg_angles(arc, 135, 45);   // 270-degree sweep, gap at the bottom
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);      // display only, no handle
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);     // taps belong to the page
        lv_obj_set_style_arc_width(arc, 16, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 16, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, COL_BAR_BG, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, COL_GREEN, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

        // The percentage is a CHILD of the arc and centred within it. Aligning
        // a sibling to the arc computes coordinates before LVGL has laid the
        // arc out, which left the digits clipped.
        *out_pct = lv_label_create(arc);
        lv_label_set_text(*out_pct, "--%");
        lv_obj_set_style_text_font(*out_pct, L.pct_font, 0);
        lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
        lv_obj_center(*out_pct);   // the dial is small now; the number fills it

        *out_bar = nullptr;
        *out_arc = arc;
    } else {
        *out_pct = lv_label_create(panel);
        lv_label_set_text(*out_pct, "---%");
        lv_obj_set_style_text_font(*out_pct, L.pct_font, 0);
        lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
        lv_obj_set_pos(*out_pct, 0, 0);
        *out_bar = make_bar(panel, 0, L.usage_bar_y,
                            w - 2 * L.panel_pad_x, L.bar_h);
        *out_arc = nullptr;
    }

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, L.rich_info ? L.detail_font : L.reset_font, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, L.usage_reset_y);

    return panel;
}

// Pairing hint — shown when disconnected so the screen isn't empty and the
// user knows how to (re)pair. Wording matches the 3-second release gesture.
static void build_pair_group(lv_obj_t* parent) {
    pair_group = lv_obj_create(parent);
    lv_obj_set_size(pair_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(pair_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(pair_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pair_group, 0, 0);
    lv_obj_set_style_pad_all(pair_group, 0, 0);
    lv_obj_clear_flag(pair_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l1 = lv_label_create(pair_group);
    lv_label_set_text(l1, "To pair");
    lv_obj_set_style_text_font(l1, L.bt_status_font, 0);
    lv_obj_set_style_text_color(l1, COL_TEXT, 0);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, L.pair_y1);

    lv_obj_t* l2 = lv_label_create(pair_group);
    lv_label_set_text(l2, "hold the power button");
    lv_obj_set_style_text_font(l2, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l2, COL_DIM, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, L.pair_y2);

    lv_obj_t* l3 = lv_label_create(pair_group);
    lv_label_set_text(l3, "for 3 seconds, then release");
    lv_obj_set_style_text_font(l3, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l3, COL_DIM, 0);
    lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, L.pair_y3);

    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);  // ui_update_ble_status decides
}

// Idle "Zzz" screen — shown when the host is connected but no usage update has
// landed recently (token expired, daemon down, host asleep…). Full-screen, like
// the pairing hint, so we never render hours-old numbers as if they were live.
static void build_idle_group(lv_obj_t* parent) {
    idle_group = lv_obj_create(parent);
    lv_obj_set_size(idle_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(idle_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(idle_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_group, 0, 0);
    lv_obj_set_style_pad_all(idle_group, 0, 0);
    lv_obj_clear_flag(idle_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    // A shrunk-down resting creature (the official cloud-ride animation)
    // sits between the header and the status line; the animated "Listening…"
    // status line carries the words, so no extra text is needed here.
    lv_obj_t* creature = splash_mini_create(idle_group, "cloud", L.idle_px);
    if (creature) lv_obj_align(creature, LV_ALIGN_CENTER, 0, -20);

    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);  // update_view_state decides
}

// ---- On-screen HID controls ----
// Boards that report button_count == 0 have no readable physical button — the
// LCD-4.3 is the first, because GPIO 0 is an RGB data line there and so the
// BOOT strap pin is an LCD output at runtime. These mirror main.cpp's
// physical-button handling exactly: PTT holds Space for as long as the finger
// is down, mode-toggle sends Shift+Tab.

// The on-screen key deck. TALK and MODE are the modal keys (hold to talk,
// cycle mode); ESC, ENTER and UP are the imperatives you reach for while
// Claude is already working — interrupt it, approve a prompt, recall the last
// one. Adding a key is a row here, not new code.
//
// Values are USB HID usage IDs; the modifier byte is the standard bitmask
// (0x02 = left shift).
struct SoftKey {
    const char* label;
    uint8_t     key;
    uint8_t     mod;
};
static const SoftKey SOFT_KEYS[] = {
    { "TALK",  0x2C, 0x00 },   // Space — held for voice-mode push-to-talk
    { "ESC",   0x29, 0x00 },   // Escape — interrupt a running response
    { "ENTER", 0x28, 0x00 },   // Return — approve a permission prompt
    { "UP",    0x52, 0x00 },   // Up arrow — recall the previous prompt
    { "MODE",  0x2B, 0x02 },   // Shift+Tab — cycle Claude Code's mode
};
#define SOFT_KEY_COUNT ((int)(sizeof(SOFT_KEYS) / sizeof(SOFT_KEYS[0])))

// One callback for every key: press on down, release on up, exactly mirroring
// how main.cpp drives physical buttons. Holding TALK therefore holds Space,
// which is what push-to-talk requires.
static void soft_key_cb(lv_event_t* e) {
    const SoftKey* k = (const SoftKey*)lv_event_get_user_data(e);
    if (!k) return;
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED)       ble_keyboard_press(k->key, k->mod);
    else if (code == LV_EVENT_RELEASED) ble_keyboard_release();
}

// Build one pill-shaped control. Not marked EVENT_BUBBLE, so its clicks never
// reach usage_container's global_click_cb and toggle the splash screen.
static lv_obj_t* make_soft_button(lv_obj_t* parent, const char* text,
                                  lv_align_t align, int16_t dx,
                                  lv_event_cb_t cb, const void* user_data) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_size(btn, L.softbtn_w, L.softbtn_h);
    lv_obj_align(btn, align, dx, L.softbtn_y);
    lv_obj_set_style_bg_color(btn, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, L.softbtn_h / 2, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, L.softbtn_font, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED, (void*)user_data);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_RELEASED, (void*)user_data);
    return btn;
}

// ---- Extra pages ----------------------------------------------------------
// Both are the same shape: a title, then PAGE_ROWS text rows laid out in two
// columns. Rows are created once and rewritten in place, so paging costs no
// allocation. Built only when L.rich_info — narrow boards have no room and
// their navigation stays splash<->usage.

static lv_obj_t* make_page(lv_obj_t* scr, const char* title, lv_obj_t** rows) {
    // A full-screen detail page, reached by tapping past the usage view. It
    // is deliberately NOT on the default screen: the main view stays the
    // canonical Clawdmeter layout and this carries everything that would
    // otherwise clutter it.
    lv_obj_t* page = lv_obj_create(scr);
    lv_obj_set_size(page, L.scr_w, L.scr_h);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    // Clearing SCROLLABLE is sufficient: lv_obj_get_scrollbar_area() returns
    // immediately for a non-scrollable object, so no bar is ever computed.
    // (An earlier comment here claimed otherwise and added a redundant
    // scrollbar-mode call — both were chasing a hairline that actually came
    // from the SCREEN's scrollbar, not the page's. See ui_init.)
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(page, global_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* t = lv_label_create(page);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &font_tiempos_34, 0);
    lv_obj_set_style_text_color(t, COL_TEXT, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);

    // Two columns across the full width: usage analysis on the left, device
    // diagnostics on the right.
    const int16_t col_w   = (L.scr_w - 2 * L.margin - L.usage_panel_gap) / 2;
    const int16_t row_h   = 27;
    const int16_t rows_y  = PAGE_TOP + 150;   // below the chart
    const int16_t per_col = PAGE_ROWS / 2;
    for (int i = 0; i < PAGE_ROWS; ++i) {
        lv_obj_t* r = lv_label_create(page);
        lv_label_set_recolor(r, true);
        lv_label_set_text(r, "");
        // Smaller than the usage page's detail font: these rows carry long
        // API-sourced values like "rejected (org level disabled)" that reach
        // the panel edge at 20 px.
        lv_obj_set_style_text_font(r, &font_styrene_16, 0);
        lv_obj_set_style_text_color(r, COL_DIM, 0);
        const bool right = (i >= per_col);
        lv_obj_set_pos(r, (int16_t)(L.margin + (right ? col_w + L.usage_panel_gap : 0)),
                          (int16_t)(rows_y + (right ? i - per_col : i) * row_h));
        rows[i] = r;
    }
    return page;
}

// "#hex label# value" — the label tinted, the value in primary text, so a row
// scans as one line without needing two widgets.
static void set_row(lv_obj_t* row, const char* label, const char* value) {
    if (!row) return;
    char buf[96];
    snprintf(buf, sizeof(buf), "#b0aea5 %s#  #faf9f5 %s#", label, value);
    lv_label_set_text(row, buf);
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, L.title_font, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    // The nudge balances the corner logo on the left; smaller on small
    // screens where the logo is 40px and the battery icon sits closer.
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);

    // Usage panels (shown when connected) live in a transparent full-size group
    // so they can be toggled against the pairing hint as one unit.
    usage_group = lv_obj_create(usage_container);
    lv_obj_set_size(usage_group, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_group, 0, 0);
    lv_obj_set_style_bg_opa(usage_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_group, 0, 0);
    lv_obj_set_style_pad_all(usage_group, 0, 0);
    lv_obj_clear_flag(usage_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    panel_session = make_usage_panel(usage_group, L.slots.p1_x, L.slots.p1_y,
                     L.slots.panel_w, "Current",
                     &lbl_session_pct, &lbl_session_label,
                     &bar_session, &lbl_session_reset, &arc_session);

    // Enterprise-only overlays inside panel_session — hidden until enterprise data arrives
    lbl_session_pct_sym = lv_label_create(panel_session);
    lv_label_set_text(lbl_session_pct_sym, "%");
    lv_obj_set_style_text_font(lbl_session_pct_sym, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_session_pct_sym, COL_TEXT, 0);
    lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_desc = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_desc, "of your monthly budget");
    lv_obj_set_style_text_font(lbl_spending_desc, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_spending_desc, COL_DIM, 0);
    lv_obj_set_pos(lbl_spending_desc, 0, L.usage_reset_y);
    lv_obj_add_flag(lbl_spending_desc, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_status = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_status, "");
    lv_obj_set_style_text_font(lbl_spending_status, L.pace_font, 0);
    lv_obj_set_pos(lbl_spending_status, 0, L.usage_reset_y + 20);
    lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);

    panel_weekly = make_usage_panel(usage_group, L.slots.p2_x, L.slots.p2_y,
                     L.slots.panel_w, "Weekly",
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset, &arc_weekly);
    // Recolor enabled so enterprise period box can color pace and reset separately
    lv_label_set_recolor(lbl_weekly_reset, true);

    // Rich-info extras: a pace/detail line inside each panel and a footer strip.
    // Recolor is on so the pace verdict can be tinted without extra widgets.
    if (L.rich_info) {
        // No pace line and no footer on the main view. The official layout is
        // percentage, pill, bar and reset — nothing else. The pace verdict,
        // burn rate and diagnostics all live on the detail page, one tap away,
        // so the screen you actually glance at stays uncluttered.
        lbl_session_detail = nullptr;
        lbl_weekly_detail  = nullptr;

        // No footer strip on the usage page: the gauge layout needs the height,
        // and the System page already reports status and data freshness. All
        // footer call sites are NULL-guarded, so leaving it unbuilt is enough.
        lbl_footer = nullptr;

        // Clock in the top-right. On narrow boards the clock replaces the
        // title, because there is nowhere else for it; here the battery slot
        // is free (this board has no battery telemetry) so we can show both
        // the title and the time. Stays empty until the daemon opts in by
        // sending wall-clock fields — see read_clock_setting() daemon-side.
        // No corner clock: in the official design the clock REPLACES the
        // title, centred, and that is what every other board does. All clock
        // call sites fall back to lbl_title when this is NULL.
        lbl_corner_clock = nullptr;
    }

    build_pair_group(usage_container);
    build_idle_group(usage_container);

    // Status line — always visible on the usage view. Driven by ui_tick_anim().
    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, L.anim_font, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, L.anim_y);
    // The split dashboard has no spare row for the animated status line: the
    // panes reach the button deck. Hidden rather than deleted so every
    // ui_tick_anim call site stays valid and narrow boards keep it.

    // Attached to usage_container rather than usage_group so they stay
    // visible across the pairing / idle / usage view states — the HID link is
    // a separate BLE connection from the daemon's, so PTT works even when no
    // usage data is flowing.
    //
    // Centred as a pair rather than pinned to the left and right edges. Edge
    // pinning is a portrait-screen habit and here it put TALK at x 20..220,
    // straight through the board's PWR hot corner (x < 72) — and because
    // touch_hal_read() hides that corner from LVGL, TALK's left third would
    // have been silently dead, cycling brightness instead of sending Space.
    // Centred, the pair spans x 188..612 on an 800 px panel, clear of both
    // corners by a wide margin.
    if (L.soft_buttons) {
        // Centred as one deck: key i sits (i - centre) steps either side of
        // the middle, so the row stays centred whatever SOFT_KEY_COUNT is.
        const int16_t step = (int16_t)(L.softbtn_w + L.softbtn_gap);
        for (int i = 0; i < SOFT_KEY_COUNT; ++i) {
            const int16_t dx = (int16_t)((i - (SOFT_KEY_COUNT - 1) / 2.0f) * step);
            make_soft_button(usage_container, SOFT_KEYS[i].label,
                             LV_ALIGN_BOTTOM_MID, dx, soft_key_cb, &SOFT_KEYS[i]);
        }
    }
}

// ======== Public API ========

void ui_init(void) {
    compute_layout(board_caps());

    lv_obj_t* scr = lv_screen_active();

    // The screen must never be scrollable. The mascot is a direct child of it
    // and deliberately parks ~27 px past the right edge while still visible
    // during walk-off trips (splash.cpp: mas_x = mas_screen_w before
    // MAS_WALK_IN). lv_obj_get_scroll_right() counts any visible, non-floating
    // child, so that overhang makes the screen horizontally scrollable and the
    // default LV_SCROLLBAR_MODE_AUTO paints a 4 px bar across y=470..473 —
    // which reads as a stray hairline at the bottom of whichever page happens
    // to be showing. It also means a drag on the background could shift the
    // whole UI sideways. Nothing here is ever meant to scroll.
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

#ifndef BOARD_HAS_PSRAM
    // Static corner mascot (see clawd_still.h) — the animated one needs PSRAM.
    if (L.small_icons) init_icon_dsc_rgb565a8(&logo_dsc, CLAWD_STILL_SMALL_W, CLAWD_STILL_SMALL_H, clawd_still_small_data);
    else               init_icon_dsc_rgb565a8(&logo_dsc, CLAWD_STILL_W, CLAWD_STILL_H, clawd_still_data);
#endif
    init_battery_icons();

    init_usage_screen(scr);
    if (L.rich_info) {
        limits_container = make_page(scr, "Detail", limits_rows);
        system_container = nullptr;   // merged into the one detail page

        // Trend chart above the stat rows. A percentage tells you where you
        // are; the shape tells you how you got there and where it's going,
        // which is the thing a single number genuinely cannot show.
        hist_chart = lv_chart_create(limits_container);
        lv_obj_set_size(hist_chart, L.scr_w - 2 * L.margin, 132);
        lv_obj_align(hist_chart, LV_ALIGN_TOP_MID, 0, PAGE_TOP);
        lv_chart_set_type(hist_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(hist_chart, HIST_POINTS);
        lv_chart_set_range(hist_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_update_mode(hist_chart, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_div_line_count(hist_chart, 5, 0);
        lv_obj_set_style_bg_color(hist_chart, COL_PANEL, 0);
        lv_obj_set_style_border_width(hist_chart, 0, 0);
        lv_obj_set_style_radius(hist_chart, 12, 0);
        lv_obj_set_style_line_color(hist_chart, COL_BAR_BG, LV_PART_MAIN);
        lv_obj_set_style_size(hist_chart, 0, 0, LV_PART_INDICATOR);  // line only, no dots
        lv_obj_clear_flag(hist_chart, LV_OBJ_FLAG_CLICKABLE);        // taps page forward
        lv_obj_set_scrollbar_mode(hist_chart, LV_SCROLLBAR_MODE_OFF);
        hist_session_ser = lv_chart_add_series(hist_chart, COL_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
        hist_weekly_ser  = lv_chart_add_series(hist_chart, COL_GREEN,  LV_CHART_AXIS_PRIMARY_Y);

        // Replay whatever survived the last power cycle, oldest first, so the
        // chart opens with real history instead of a blank panel.
        history_init();
        const uint16_t restored = history_count();
        for (uint16_t i = 0; i < restored; ++i) {
            uint8_t sp = 0, wp = 0;
            if (!history_get(i, &sp, &wp)) break;
            lv_chart_set_next_value(hist_chart, hist_session_ser, sp);
            lv_chart_set_next_value(hist_chart, hist_weekly_ser,  wp);
        }
        hist_samples = restored;

        // Shown only until there are two points to draw a line between. Never
        // seed the series with the current value to fill the gap — a flat line
        // would imply hours of history we do not have.
        hist_hint = lv_label_create(limits_container);
        lv_label_set_text(hist_hint, "collecting - one point per minute");
        lv_obj_set_style_text_font(hist_hint, L.detail_font, 0);
        lv_obj_set_style_text_color(hist_hint, COL_DIM, 0);
        lv_obj_align_to(hist_hint, hist_chart, LV_ALIGN_CENTER, 0, 0);

        // The chart takes the space make_page gave the rows, so re-lay the
        // first four beneath it (2x2) and hide the rest. Four is deliberate:
        // these are the figures that say something the chart doesn't.
        for (int i = 0; i < PAGE_ROWS; ++i) {
            if (i < PAGE_ROWS) {
            } else {
                lv_obj_add_flag(limits_rows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    splash_init(scr);

    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }

    // Corner mascot in the old logo slot. The still Clawd is shorter than the
    // 80/40 px slot the spark logo used; center it vertically in that slot.
    {
        const int slot  = L.small_icons ? LOGO_SMALL_HEIGHT : LOGO_HEIGHT;
        const int art_h = L.small_icons ? CLAWD_STILL_SMALL_H : CLAWD_STILL_H;
        const int top   = L.logo_y + (slot - art_h) / 2;
#ifdef BOARD_HAS_PSRAM
        // Animated: idles, does acts, and takes walk-off/lurk trips.
        splash_mascot_create(scr, L.margin, top + art_h, L.small_icons ? 2 : 3);
#else
        logo_img = lv_image_create(scr);
        lv_image_set_src(logo_img, &logo_dsc);
        lv_obj_set_pos(logo_img, L.margin, top);
#endif
    }

    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, L.scr_w - L.batt_w - L.margin, L.batt_y);
    // Boards without battery telemetry never show the indicator (per the HAL
    // contract; previously every board drew the empty-battery glyph).
    if (!board_caps().has_battery) {
        lv_obj_del(battery_img);
        battery_img = nullptr;
    }
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;
    data_ok = data->ok;
    if (!data->ok) return;          // a {"ok":false} "no data" beat → fall through to idle, keep last numbers
    last_data_ms = lv_tick_get();   // a real usage update just landed
    data_received = true;

    if (data->clock_epoch > 0) {    // daemon supplied wall-clock time → drive the title clock
        clock_base_epoch = data->clock_epoch;
        clock_base_ms = last_data_ms;
        clock_fmt = data->clock_fmt;
    } else if (clock_base_epoch != 0) {   // clock turned off daemon-side → revert title to "Usage"
        clock_base_epoch = 0;
        clock_last_min = -1;
        lv_label_set_text(lbl_title, "Usage");
    }

    int s_pct = (int)(data->session_pct + 0.5f);

    if (data->enterprise) {
        // Spending box: big number-only label + small "%" symbol + desc + pace
        lv_obj_set_style_text_font(lbl_session_pct, L.ent_pct_font, 0);
        lv_label_set_text(lbl_session_label, "Spending");
        lv_obj_add_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status,   LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_text_font(lbl_session_pct, L.pct_font, 0);
        lv_label_set_text(lbl_session_label, "Current");
        lv_obj_clear_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    }

    char buf[48];

    // Pace vars used in both enterprise blocks below
    const char* pace_text = "Under pace";
    lv_color_t  pace_color = COL_GREEN;
    const char* pace_hex   = "788c5d";   // matches THEME_GREEN
    if (data->session_pct > (float)data->time_pct + 15.0f) {
        pace_text = "Over pace";  pace_color = COL_RED;   pace_hex = "c0392b";
    } else if (data->session_pct > (float)data->time_pct - 15.0f) {
        pace_text = "On pace";    pace_color = COL_AMBER; pace_hex = "d97757";
    }

    if (data->enterprise) {
        lv_label_set_text_fmt(lbl_session_pct, "%d", s_pct);
        lv_obj_align_to(lbl_session_pct_sym, lbl_session_pct,
                        LV_ALIGN_OUT_RIGHT_TOP, 4, 12);
    } else {
        lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
        format_reset_time(data->session_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_session_reset, buf);
    }

    if (bar_session) {
        lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);
    }
    if (arc_session) {
        lv_arc_set_value(arc_session, s_pct);
        // Tint by pace, not by absolute level: 40% used is fine 90% into the
        // window and alarming 10% in. The dial then answers "am I ahead or
        // behind" at a glance, which the raw percentage cannot.
        lv_obj_set_style_arc_color(arc_session,
            pace_color_for(data->session_pct, data->session_reset_mins,
                           data->session_window_mins),
            LV_PART_INDICATOR);
    }

    if (data->enterprise) {
        // Period box: time % + dynamic pace color + "Resets <date>" label
        lv_label_set_text(lbl_weekly_label, "Period");
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", data->time_pct);
        lv_bar_set_value(bar_weekly, data->time_pct, LV_ANIM_ON);
        lv_color_t bar_pace = (data->session_pct <= (float)data->time_pct) ? COL_GREEN :
                              (data->session_pct <= (float)data->time_pct + 15.0f) ? COL_AMBER :
                              COL_RED;
        lv_obj_set_style_bg_color(bar_weekly, bar_pace, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "#%s %s# - #faf9f5 Resets %s#",
                 pace_hex, pace_text, data->reset_date);
        lv_label_set_text(lbl_weekly_reset, buf);
    } else {
        int w_pct = (int)(data->weekly_pct + 0.5f);
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
        if (bar_weekly) {
            lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
            lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);
        }
        if (arc_weekly) {
            lv_arc_set_value(arc_weekly, w_pct);
            lv_obj_set_style_arc_color(arc_weekly,
                pace_color_for(data->weekly_pct, data->weekly_reset_mins,
                               data->weekly_window_mins),
                LV_PART_INDICATOR);
        }
        format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_weekly_reset, buf);
    }

    // Rich-info extras. Enterprise already carries its own pace verdict in the
    // period box, so the per-panel detail is Pro/Max only; the footer applies
    // to both.
    s_last_data = *data;
    // One point per payload; LVGL shifts the oldest off the left edge.
    if (hist_chart) {
        const uint8_t sp = (uint8_t)(data->session_pct + 0.5f);
        const uint8_t wp = (uint8_t)(data->weekly_pct + 0.5f);
        lv_chart_set_next_value(hist_chart, hist_session_ser, sp);
        lv_chart_set_next_value(hist_chart, hist_weekly_ser,  wp);
        history_push(sp, wp);   // persistence is rate-limited inside the store
        if (hist_samples < 0xFFFF) hist_samples++;
        // Two points make a line; below that the chart has nothing to show.
        if (hist_hint && hist_samples >= 2) lv_obj_add_flag(hist_hint, LV_OBJ_FLAG_HIDDEN);
    }
    s_last_enterprise = data->enterprise;
    strlcpy(s_last_status, data->status, sizeof(s_last_status));
    if (lbl_session_detail && !data->enterprise) {
        // Window lengths are supplied by the daemon (parsed from the API's own
        // header names), never assumed here. A 0 means the API named a window
        // we don't recognise, in which case format_pace_detail blanks the line
        // rather than printing a confident wrong percentage.
        format_pace_detail(data->session_pct, data->session_reset_mins,
                           data->session_window_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_session_detail, buf);
        format_pace_detail(data->weekly_pct, data->weekly_reset_mins,
                           data->weekly_window_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_weekly_detail, buf);
    }
    if (lbl_footer) {
        // No tier label for non-enterprise accounts. The daemon hardcodes
        // "acct":"pro" to mean "not an enterprise spending-limit account", and
        // neither it nor ~/.claude.json can distinguish Pro from Max (seatTier
        // is null, billingType is just "stripe_subscription"). Printing "Pro"
        // would be a guess shown as a fact to every Max subscriber.
        snprintf(buf, sizeof(buf), "%s%s - updated just now",
                 data->enterprise ? "Enterprise - " : "", data->status);
        lv_label_set_text(lbl_footer, buf);
    }
}

// Repaint the Limits and System pages. Driven from ui_tick_anim rather than
// ui_update so the live figures (uptime, data age, projections) keep moving
// between the daemon's 60s payloads — a frozen diagnostic page is worse than
// none, because it looks authoritative.
static void refresh_pages(void) {
    if (!limits_container) return;
    char v[64];
    const UsageData* d = &s_last_data;

    // ---- History: four figures the chart itself cannot show ----
    if (d->valid) {
        const float rate = usage_rate_pct_per_hour();
        if (rate < 0.0f) snprintf(v, sizeof(v), "warming up");
        else             snprintf(v, sizeof(v), "%d.%d %%/hr", (int)rate,
                                  (int)((rate < 0 ? -rate : rate) * 10) % 10);
        set_row(limits_rows[0], "Burn rate", v);

        const int to_full = usage_rate_mins_to_full();
        if (to_full == -1)      snprintf(v, sizeof(v), "warming up");
        else if (to_full == -2) snprintf(v, sizeof(v), "steady, not climbing");
        else if (to_full < 60)  snprintf(v, sizeof(v), "limit in %dm", to_full);
        else                    snprintf(v, sizeof(v), "limit in %dh %dm", to_full / 60, to_full % 60);
        set_row(limits_rows[1], "Projected", v);

        // The API's own statement of which window is currently binding.
        set_row(limits_rows[2], "Binding", d->claim[0] ? d->claim : "unknown");

        if (d->overage[0]) {
            if (d->overage_reason[0]) {
                // API reasons arrive snake_cased ("org_level_disabled");
                // humanise rather than reformat upstream's value.
                char reason[24];
                strlcpy(reason, d->overage_reason, sizeof(reason));
                for (char* p = reason; *p; ++p) if (*p == '_') *p = ' ';
                snprintf(v, sizeof(v), "%s (%s)", d->overage, reason);
            } else {
                snprintf(v, sizeof(v), "%s", d->overage);
            }
        } else snprintf(v, sizeof(v), "not reported");
        set_row(limits_rows[3], "Overage", v);

        set_row(limits_rows[4], "Status", d->status);
        set_row(limits_rows[5], "Weekly", d->weekly_status[0] ? d->weekly_status : "-");
    }

    // ---- Right column: device + link diagnostics ----
    const BoardCaps& c = board_caps();
    set_row(limits_rows[6], "Board", c.name);

    const uint32_t up_s = millis() / 1000;
    if (up_s < 3600) snprintf(v, sizeof(v), "%lum %lus", (unsigned long)(up_s / 60), (unsigned long)(up_s % 60));
    else             snprintf(v, sizeof(v), "%luh %lum", (unsigned long)(up_s / 3600), (unsigned long)((up_s % 3600) / 60));
    set_row(limits_rows[7], "Uptime", v);

    snprintf(v, sizeof(v), "%lu KB", (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    set_row(limits_rows[8], "Free RAM", v);
#ifdef BOARD_HAS_PSRAM
    snprintf(v, sizeof(v), "%lu KB", (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    set_row(limits_rows[9], "Free PSRAM", v);
#else
    set_row(limits_rows[9], "Free PSRAM", "none");
#endif

    set_row(limits_rows[10], "Link",
            s_ble_connected ? ble_get_mac_address() : "disconnected");

    if (last_data_ms == 0) snprintf(v, sizeof(v), "no data yet");
    else {
        const uint32_t age = (lv_tick_get() - last_data_ms) / 1000;
        if (age < 90) snprintf(v, sizeof(v), "%lus ago", (unsigned long)age);
        else          snprintf(v, sizeof(v), "%lum ago", (unsigned long)(age / 60));
    }
    set_row(limits_rows[11], "Updated", v);
}

// Pick the usage-view sub-screen: pairing hint (BLE down), the idle "Zzz" screen
// (connected but data has gone stale), or the live usage panels. Only re-lays-out
// on an actual change. The animated status line stays visible everywhere — it
// reads "Listening…" on the idle screen, keeping it alive rather than frozen.
static void update_view_state(void) {
    if (!usage_group || !pair_group || !idle_group) return;
    int v;
    if (!s_ble_connected) {
        v = 0;  // pairing hint
    } else if (data_received && data_ok && (lv_tick_get() - last_data_ms) < DATA_FRESH_MS) {
        v = 2;  // live usage
    } else {
        v = 1;  // idle / Zzz
    }
    if (v == view_state) return;
    view_state = v;
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v == 0 ? pair_group : v == 1 ? idle_group : usage_group,
                      LV_OBJ_FLAG_HIDDEN);
}

void ui_tick_anim(void) {
    // Every dashboard state shows the gauges, so they all need the usage
    // animations AND the pane refresh — uptime, data age and the projection
    // have to keep moving between the daemon's 60s payloads.
    const bool dashboard = (current_screen == SCREEN_USAGE ||
                            current_screen == SCREEN_LIMITS ||
                            current_screen == SCREEN_SYSTEM);
    if (!dashboard) return;

    if (limits_container) {
        static uint32_t pages_last_s = 0xFFFFFFFF;
        const uint32_t s = lv_tick_get() / 1000;
        if (s != pages_last_s) {
            pages_last_s = s;
            refresh_pages();
        }
    }
    update_view_state();
    if (view_state == 1) splash_mini_tick();   // animate the sleeping creature on the idle screen

    uint32_t now = lv_tick_get();

    // Footer freshness. Recomputed here rather than in ui_update so a stalled
    // daemon is visible: the age keeps climbing instead of freezing at the last
    // successful poll, which is the whole point of showing it.
    if (lbl_footer && last_data_ms != 0) {
        static uint32_t footer_last_s = 0xFFFFFFFF;
        const uint32_t age_s = (now - last_data_ms) / 1000;
        if (age_s != footer_last_s) {
            footer_last_s = age_s;
            const char* tier = s_last_enterprise ? "Enterprise - " : "";
            char fbuf[64];
            if (age_s < 5)        snprintf(fbuf, sizeof(fbuf), "%s%s - updated just now", tier, s_last_status);
            else if (age_s < 90)  snprintf(fbuf, sizeof(fbuf), "%s%s - updated %lus ago", tier, s_last_status, (unsigned long)age_s);
            else                  snprintf(fbuf, sizeof(fbuf), "%s%s - updated %lum ago", tier, s_last_status, (unsigned long)(age_s / 60));
            lv_label_set_text(lbl_footer, fbuf);
        }
    }

    // Title clock: once the daemon has sent wall-clock time, replace "Usage" with
    // the live time, advanced locally so it ticks every minute between payloads.
    if (clock_base_epoch > 0) {
        time_t cur = (time_t)(clock_base_epoch + (now - clock_base_ms) / 1000);
        struct tm tmv;
        gmtime_r(&cur, &tmv);   // epoch is already local wall-clock → gmtime keeps it as-is
        if (tmv.tm_min != clock_last_min) {   // only rewrite the title when the minute changes
            clock_last_min = tmv.tm_min;
            char tbuf[12];
            if (clock_fmt == 12) {
                int h12 = tmv.tm_hour % 12;
                if (h12 == 0) h12 = 12;
                snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, tmv.tm_min,
                         tmv.tm_hour < 12 ? "AM" : "PM");
            } else {
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            }
            // Wide boards show the clock beside the title rather than instead
            // of it — there is a free corner, so we don't have to choose.
            lv_label_set_text(lbl_corner_clock ? lbl_corner_clock : lbl_title, tbuf);
        }
    }

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms < spinner_ms[anim_spinner_idx]) return;
    anim_last_ms = now;
    anim_phase = (anim_phase + 1) % SPINNER_PHASES;
    anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                    : (SPINNER_PHASES - anim_phase);

    // Status text by priority. Whimsical messages only when connected & settled.
    const char* text;
    if (!s_ble_connected) {
        text = "Waiting";              // advertising / waiting for a host connection
    } else if (view_state == 1) {      // idle — alternate so it reads as alive AND data-less
        text = (anim_msg_idx & 1) ? "No data" : "Listening";
    } else if (now - connected_at_ms < 5000) {
        text = "Connected";
    } else {
        text = anim_messages[anim_msg_idx];
    }

    // All states share the whimsical style: "<glyph> <Title-case word>…"
    static char buf[80];
    snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
             spinner_frames[anim_spinner_idx], text);
    lv_label_set_text(lbl_anim, buf);
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;
static void apply_battery_visibility(void) {
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    else                                  lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
}

static void global_click_cb(lv_event_t* e) {
    (void)e;
    // Boards with the extra pages cycle through them; boards without keep the
    // original two-state toggle, since limits_container is NULL there.
    if (!limits_container) {
        if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
        else                                  ui_show_screen(SCREEN_SPLASH);
        return;
    }
    // The gauges are always on screen in every dashboard state, so there is no
    // separate "usage only" stop in the cycle — tapping just swaps the pane
    // beside them, then returns to the splash.
    switch (current_screen) {
    case SCREEN_SPLASH: ui_show_screen(SCREEN_USAGE);  break;
    case SCREEN_USAGE:  ui_show_screen(SCREEN_LIMITS); break;
    default:            ui_show_screen(SCREEN_SPLASH); break;
    }
}

void ui_show_screen(screen_t screen) {
    // Requesting a page this board never built (narrow panels) falls back to
    // the usage view rather than showing a blank screen.
    if ((screen == SCREEN_LIMITS || screen == SCREEN_SYSTEM) && !limits_container) {
        screen = SCREEN_USAGE;
    }

    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    if (limits_container) lv_obj_add_flag(limits_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:  splash_show(); break;
    case SCREEN_USAGE:   lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_LIMITS:  lv_obj_clear_flag(limits_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    splash_mascot_set_visible(screen != SCREEN_SPLASH);
    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    apply_battery_visibility();
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(ble_state_t state, const char* name, const char* mac) {
    (void)name; (void)mac;
    bool was_connected = s_ble_connected;
    s_ble_connected = (state == BLE_STATE_CONNECTED);

    if (s_ble_connected && !was_connected) connected_at_ms = lv_tick_get();
    // pair / idle / usage — picked from connection + data freshness.
    update_view_state();
}

void ui_update_battery(int percent, bool charging) {
    if (!battery_img) return;
    int idx;
    if (charging) {
        idx = 4;
    } else if (percent < 0) {
        idx = 0;
    } else if (percent <= 10) {
        idx = 0;
    } else if (percent <= 35) {
        idx = 1;
    } else if (percent <= 75) {
        idx = 2;
    } else {
        idx = 3;
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
}
