#include "../../hal/display_hal.h"
#include "board.h"
#include "ch422g.h"
#include "dimming.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// 800x480 RGB parallel panel — the first non-serial display bus in the tree.
// There is no panel-side GRAM: the ESP32-S3 LCD peripheral re-scans a 768 KB
// PSRAM framebuffer continuously, so draw_bitmap is a copy into RAM rather
// than a bus transaction, and the panel is "always drawing" whether or not we
// push anything.
//
// LVGL stays in PARTIAL render mode (see main.cpp) and its strips are copied
// into that framebuffer. DIRECT mode would be zero-copy and is entirely
// practical — Arduino_ESP32RGBPanel exposes getFrameBuffer() — but it would
// require main.cpp to hand LVGL a board-owned pointer, and shared code does
// not change for one board.

static Arduino_ESP32RGBPanel* panel = nullptr;
static Arduino_RGB_Display*   gfx   = nullptr;

// Software brightness. The backlight is a single on/off line, so levels are
// synthesised by scaling pixels on their way into the framebuffer. Level 255
// skips the LUT entirely and takes the plain fast copy.
static DimLut   dim_lut;
static uint8_t  dim_level = 255;
static bool     dim_dirty = false;       // level changed; framebuffer is stale
static uint16_t row_scratch[LCD_WIDTH];  // one dimmed row; bounded and small

#define DIM_BACKLIGHT_OFF_BELOW 8

void display_hal_init(void) {
    panel = new Arduino_ESP32RGBPanel(
        LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
        LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
        LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
        LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
        LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT, LCD_HSYNC_PULSE, LCD_HSYNC_BACK,
        LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT, LCD_VSYNC_PULSE, LCD_VSYNC_BACK,
        LCD_PCLK_ACTIVE_NEG, LCD_PREFER_SPEED, false /* useBigEndian */,
        0 /* de_idle_high */, 0 /* pclk_idle_high */, LCD_BOUNCE_BUF_PX);

    gfx = new Arduino_RGB_Display(LCD_WIDTH, LCD_HEIGHT, panel,
                                  0 /* rotation */, true /* auto_flush */);
}

void display_hal_begin(void) {
    if (!gfx || !gfx->begin()) {
        // The RGB peripheral will happily scan an unallocated framebuffer and
        // show noise rather than crash, so say so loudly.
        Serial.println("Display: gfx->begin() FAILED (framebuffer alloc?)");
        return;
    }
    gfx->fillScreen(0x0000);
    ch422g_set(EXIO_LCD_BL, true);
    Serial.printf("Display OK (%dx%d RGB parallel)\n", LCD_WIDTH, LCD_HEIGHT);
}

void display_hal_set_brightness(uint8_t level) {
    if (level == dim_level) return;
    dim_level = level;

    if (level < DIM_BACKLIGHT_OFF_BELOW) {
        // Bottom of the range is a genuine blank: cut EXIO2 so the backlight
        // draws no power. This is where the idle timeout lands.
        ch422g_set(EXIO_LCD_BL, false);
        return;
    }

    dim_lut_build(&dim_lut, level);
    ch422g_set(EXIO_LCD_BL, true);
    // The framebuffer holds pixels written at the previous level, so it has
    // to be repainted. Defer to display_hal_tick — set_brightness can be
    // called before LVGL exists (main.cpp calls brightness_init() before
    // lv_init()).
    dim_dirty = true;
}

void display_hal_fill_screen(uint16_t color565) {
    if (gfx) gfx->fillScreen(color565);
}

void display_hal_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                             const uint16_t* pixels) {
    if (!gfx) return;

    if (dim_level == 255 || w <= 0 || w > LCD_WIDTH) {
        gfx->draw16bitRGBBitmap(x, y, (uint16_t*)pixels, w, h);
        return;
    }

    // Dim a row at a time into a fixed scratch buffer: bounded memory, no
    // allocation, and h is at most the LVGL strip height (40 rows).
    for (int32_t row = 0; row < h; ++row) {
        const uint16_t* src = pixels + (size_t)row * (size_t)w;
        for (int32_t i = 0; i < w; ++i) row_scratch[i] = dim_rgb565(&dim_lut, src[i]);
        gfx->draw16bitRGBBitmap(x, y + row, row_scratch, w, 1);
    }
}

void display_hal_tick(void) {
    // A brightness change leaves the framebuffer holding pixels written at
    // the old level, so force a repaint once LVGL is up. Same pattern the
    // 2.16 port uses for its rotation transition.
    if (dim_dirty && lv_is_initialized()) {
        dim_dirty = false;
        lv_obj_invalidate(lv_screen_active());
    }
}

void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    // RGB parallel panels have no even-alignment requirement — the framebuffer
    // is plain RAM.
    (void)x1; (void)y1; (void)x2; (void)y2;
}
