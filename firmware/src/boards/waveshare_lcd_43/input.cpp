#include "../../hal/input_hal.h"

// No readable physical buttons: GPIO 0 (BOOT) is the G3 RGB data line and is
// driven by the LCD peripheral. HID Space / Shift+Tab come from the
// on-screen buttons ui.cpp builds when board_caps().button_count == 0.

void input_hal_init(void) {}

bool input_hal_is_held(InputButton btn) {
    (void)btn;
    return false;
}
