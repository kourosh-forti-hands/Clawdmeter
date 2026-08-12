#pragma once
#include <stdint.h>

// Pure placement of the two usage panels. Deliberately free of LVGL/Arduino
// dependencies so it can be unit-tested on the host, matching
// splash_geometry.h.
//
// Every board through the LCD-1.54 stacks the panels vertically at full
// content width. The LCD-4.3 is the first landscape panel (800x480) — stacking
// there would leave two very wide, very short panels above a large empty
// region, so wide screens get the panels side by side instead.
//
// The 700 px threshold is deliberately above the widest existing board (480)
// so no current port changes behaviour, and the aspect check keeps tall
// panels stacked regardless of absolute width.

#define USAGE_TWO_COL_MIN_W 700

typedef struct {
    int16_t p1_x, p1_y;   // first panel (session / "Current")
    int16_t p2_x, p2_y;   // second panel (weekly)
    int16_t panel_w;      // width shared by both panels
    bool    wide_split;   // true on wide landscape: panels occupy the LEFT half
    int16_t pane_x;       // left edge of the free right-hand pane (wide_split)
    int16_t pane_w;       // width of that pane; 0 when there isn't one
} UsageSlots;

static inline UsageSlots usage_compute_slots(int16_t scr_w, int16_t scr_h,
                                             int16_t margin, int16_t content_y,
                                             int16_t panel_h, int16_t gap) {
    UsageSlots s;
    s.wide_split = (scr_w >= USAGE_TWO_COL_MIN_W) && (scr_w > scr_h);

    // Both panels always stack vertically. What a wide landscape screen buys
    // is not a second panel column but a second *region*: the panels take the
    // left half and the right half is freed for a pane that cycles through
    // History/System, so usage is never off-screen and detail is one tap away
    // instead of three.
    s.panel_w = s.wide_split ? (int16_t)((scr_w - 2 * margin - gap) / 2)
                             : (int16_t)(scr_w - 2 * margin);
    s.p1_x = margin;
    s.p1_y = content_y;
    s.p2_x = margin;
    s.p2_y = (int16_t)(content_y + panel_h + gap);

    if (s.wide_split) {
        s.pane_x = (int16_t)(margin + s.panel_w + gap);
        s.pane_w = (int16_t)(scr_w - s.pane_x - margin);
    } else {
        s.pane_x = 0;
        s.pane_w = 0;
    }
    return s;
}
