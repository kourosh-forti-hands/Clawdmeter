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
} UsageSlots;

// Both panels always stack vertically at full content width, on every board.
// That is the canonical Clawdmeter layout (see screenshots/usage.png) and a
// wide screen does not change it — earlier revisions tried a two-column split
// and then a fixed-left/cycling-right dashboard, and both traded the design's
// clarity for density that belongs on a second screen instead.
static inline UsageSlots usage_compute_slots(int16_t scr_w, int16_t scr_h,
                                             int16_t margin, int16_t content_y,
                                             int16_t panel_h, int16_t gap) {
    (void)scr_h;
    UsageSlots s;
    s.panel_w = (int16_t)(scr_w - 2 * margin);
    s.p1_x = margin;
    s.p1_y = content_y;
    s.p2_x = margin;
    s.p2_y = (int16_t)(content_y + panel_h + gap);
    return s;
}
