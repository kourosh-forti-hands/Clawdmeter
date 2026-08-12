// Host unit test for the RGB565 brightness LUT — the pure arithmetic behind
// software dimming on a panel whose backlight is on/off only. No Arduino or
// LVGL deps, so it runs anywhere:
//
//   g++ -std=c++17 -I ../../src test_main.cpp -o t && ./t

#include "boards/waveshare_lcd_43/dimming.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void should_be_identity_at_full_brightness() {
    DimLut t;
    dim_lut_build(&t, 255);
    // Every representable pixel must survive untouched, or full brightness
    // would silently shift colour.
    for (uint32_t px = 0; px <= 0xFFFF; ++px) {
        if (dim_rgb565(&t, (uint16_t)px) != (uint16_t)px) {
            printf("FAIL identity at 0x%04X -> 0x%04X\n",
                   (unsigned)px, dim_rgb565(&t, (uint16_t)px));
            ++failures;
            return;
        }
    }
}

static void should_be_black_at_zero() {
    DimLut t;
    dim_lut_build(&t, 0);
    CHECK(dim_rgb565(&t, 0xFFFF) == 0x0000);
    CHECK(dim_rgb565(&t, 0xF800) == 0x0000);
    CHECK(dim_rgb565(&t, 0x07E0) == 0x0000);
    CHECK(dim_rgb565(&t, 0x001F) == 0x0000);
}

static void should_halve_each_channel_at_half_brightness() {
    DimLut t;
    dim_lut_build(&t, 128);
    // White (31, 63, 31) -> (15, 31, 15) with integer truncation.
    uint16_t out = dim_rgb565(&t, 0xFFFF);
    CHECK(((out >> 11) & 0x1F) == 15);
    CHECK(((out >> 5) & 0x3F) == 31);
    CHECK((out & 0x1F) == 15);
}

static void should_not_bleed_between_channels() {
    DimLut t;
    dim_lut_build(&t, 128);
    // Pure red must stay pure red — a shift/mask bug shows up as green or
    // blue appearing where there was none.
    uint16_t out = dim_rgb565(&t, 0xF800);
    CHECK(((out >> 5) & 0x3F) == 0);
    CHECK((out & 0x1F) == 0);
    CHECK(((out >> 11) & 0x1F) == 15);
}

static void should_be_monotonic_in_level() {
    // Raising the level must never darken a pixel, or the brightness cycle
    // would feel non-deterministic.
    uint16_t prev = 0;
    for (int level = 0; level <= 255; ++level) {
        DimLut t;
        dim_lut_build(&t, (uint8_t)level);
        uint16_t cur = (uint16_t)((dim_rgb565(&t, 0xFFFF) >> 11) & 0x1F);
        CHECK(cur >= prev);
        prev = cur;
    }
}

int main() {
    should_be_identity_at_full_brightness();
    should_be_black_at_zero();
    should_halve_each_channel_at_half_brightness();
    should_not_bleed_between_channels();
    should_be_monotonic_in_level();
    if (failures == 0) printf("all dimming tests passed\n");
    return failures != 0;
}
