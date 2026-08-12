#pragma once
#include <stdint.h>

// Board-private hook so power.cpp can see touches that touch_hal_read() hides
// from LVGL. Both files are inside the board folder, so this coupling stays
// local — shared code never sees it.
void touch_raw_read(uint16_t* x, uint16_t* y, bool* pressed);
