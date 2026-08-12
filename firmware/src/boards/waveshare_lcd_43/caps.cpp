#include "../../hal/board_caps.h"
#include "board.h"

// button_count = 0 is new: every other port has at least the BOOT button.
// GPIO 0 is an RGB data line here, so there is nothing to read. main.cpp
// already gates the secondary-button HID on button_count >= 2; ui.cpp gates
// the on-screen TALK/MODE buttons on button_count == 0.
static const BoardCaps caps = {
    .name = BOARD_NAME,
    .width = LCD_WIDTH,
    .height = LCD_HEIGHT,
    .button_count = 0,
    .has_rotation = (bool)BOARD_HAS_ROTATION,
    .has_battery  = (bool)BOARD_HAS_BATTERY,
    .has_imu      = (bool)BOARD_HAS_IMU,
};

const BoardCaps& board_caps(void) { return caps; }
