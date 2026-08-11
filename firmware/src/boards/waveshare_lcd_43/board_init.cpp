#include "board.h"
#include <Arduino.h>
#include <Wire.h>

// Task 3 adds the CH422G bring-up that must happen before display_hal_init():
// backlight enable on EXIO2 and the touch reset pulse on EXIO1.
extern "C" void board_init(void) {
    Wire.begin(IIC_SDA, IIC_SCL);
}
