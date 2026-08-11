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
    bool    two_col;      // true when placed side by side
} UsageSlots;

static inline UsageSlots usage_compute_slots(int16_t scr_w, int16_t scr_h,
                                             int16_t margin, int16_t content_y,
                                             int16_t panel_h, int16_t gap) {
    UsageSlots s;
    s.two_col = (scr_w >= USAGE_TWO_COL_MIN_W) && (scr_w > scr_h);

    if (s.two_col) {
        s.panel_w = (int16_t)((scr_w - 2 * margin - gap) / 2);
        s.p1_x = margin;
        s.p1_y = content_y;
        s.p2_x = (int16_t)(margin + s.panel_w + gap);
        s.p2_y = content_y;
    } else {
        s.panel_w = (int16_t)(scr_w - 2 * margin);
        s.p1_x = margin;
        s.p1_y = content_y;
        s.p2_x = margin;
        s.p2_y = (int16_t)(content_y + panel_h + gap);
    }
    return s;
}
