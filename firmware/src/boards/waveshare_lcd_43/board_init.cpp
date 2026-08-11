#include "board.h"
#include "ch422g.h"
#include <Arduino.h>
#include <Wire.h>

// Per-board pre-init. Everything here must complete before
// display_hal_init(), because EXIO2 gates the backlight and EXIO1 gates the
// touch controller's reset.

// Bring-up note (scan removed after verification): on this board the I2C scan
// ACKs 0x20-0x27 and 0x30-0x3F — that whole range is the single CH422G, whose
// addresses ARE its registers — plus 0x5D for the GT911. If touch ever stops
// responding, check for 0x5D first; 0x14 would mean the INT-low-during-reset
// address selection below did not take.

extern "C" void board_init(void) {
    Wire.begin(IIC_SDA, IIC_SCL);
    delay(50);            // let the expander and touch controller settle

    ch422g_init();

    // Backlight on. display.cpp takes ownership of this line for idle
    // blanking once brightness is under its control.
    ch422g_set(EXIO_LCD_BL, true);

    // Touch reset pulse. WR_IO powers up at 0xFF so the GT911 is already out
    // of reset — drive EXIO1 low first or the controller is never reset.
    // INT is held low across the release to select address 0x5D.
    pinMode(TP_INT, OUTPUT);
    digitalWrite(TP_INT, LOW);
    ch422g_set(EXIO_TP_RST, false);
    delay(10);
    ch422g_set(EXIO_TP_RST, true);
    delay(5);
    pinMode(TP_INT, INPUT);
    delay(50);            // GT911 firmware boot
    Serial.println("board_init done");
}
