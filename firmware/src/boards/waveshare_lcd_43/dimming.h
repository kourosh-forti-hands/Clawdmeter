#pragma once
#include <stdint.h>

// Pure RGB565 brightness scaling. Deliberately free of Arduino/LVGL deps so
// it can be unit-tested on the host (see test/test_lcd43_dimming/), matching
// the src/splash_geometry.h precedent.
//
// Why software dimming at all: this board's backlight is a single on/off line
// on the CH422G expander (EXIO2 -> DISP), so there is no PWM duty to vary.
// Scaling pixels on their way into the framebuffer is the only way to get
// graduated brightness, and display_hal_draw_bitmap is the one function both
// LVGL flushes and splash.cpp's direct blits pass through.
//
// Two tables rather than three: red and blue are both 5-bit, green is 6-bit.

typedef struct {
    uint8_t l5[32];
    uint8_t l6[64];
} DimLut;

static inline void dim_lut_build(DimLut* t, uint8_t level) {
    for (int i = 0; i < 32; ++i) t->l5[i] = (uint8_t)((i * level) / 255);
    for (int i = 0; i < 64; ++i) t->l6[i] = (uint8_t)((i * level) / 255);
}

static inline uint16_t dim_rgb565(const DimLut* t, uint16_t px) {
    uint16_t r = (uint16_t)((px >> 11) & 0x1F);
    uint16_t g = (uint16_t)((px >> 5) & 0x3F);
    uint16_t b = (uint16_t)(px & 0x1F);
    return (uint16_t)(((uint16_t)t->l5[r] << 11) |
                      ((uint16_t)t->l6[g] << 5) |
                       (uint16_t)t->l5[b]);
}
