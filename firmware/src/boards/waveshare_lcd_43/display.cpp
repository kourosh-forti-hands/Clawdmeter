#include "../../hal/display_hal.h"

// Task 4 replaces these with the Arduino_ESP32RGBPanel construction.

void display_hal_init(void) {}
void display_hal_begin(void) {}
void display_hal_set_brightness(uint8_t level) { (void)level; }
void display_hal_fill_screen(uint16_t color565) { (void)color565; }

void display_hal_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                             const uint16_t* pixels) {
    (void)x; (void)y; (void)w; (void)h; (void)pixels;
}

void display_hal_tick(void) {}

void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
}
