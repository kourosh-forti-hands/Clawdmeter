#pragma once
#include <stdint.h>
#include "board.h"

// Hit region for the virtual PWR button. This board has no readable physical
// button (GPIO 0 is the G3 RGB data line), so power.cpp synthesises PWR
// press/long-press/release from a square of glass in the bottom-left corner.
// touch.cpp hides the same region from LVGL so a PWR tap does not also fire
// ui.cpp's global_click_cb and toggle the splash screen.
//
// Pure and header-only so the geometry is unit-tested on the host.

// TOP-RIGHT, not bottom-left. The five-key HID deck spans the full width at
// the bottom of the screen, so a bottom-left corner sits directly on top of
// TALK — and because touch_hal_read() hides this region from LVGL, that made
// TALK's left third silently dead. The top-right corner is free: the panels
// start below it and the clock is centred.
static inline bool pwr_corner_contains(int16_t x, int16_t y, int16_t screen_w,
                                       int16_t screen_h) {
    (void)screen_h;
    return x >= (int16_t)(screen_w - (int16_t)PWR_CORNER_PX) &&
           y < (int16_t)PWR_CORNER_PX;
}
