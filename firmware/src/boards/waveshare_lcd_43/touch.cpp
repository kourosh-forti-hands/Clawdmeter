#include "../../hal/touch_hal.h"

// Task 6 replaces these with the vendored GT911 reader.

void touch_hal_init(void) {}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    *x = 0;
    *y = 0;
    *pressed = false;
}
