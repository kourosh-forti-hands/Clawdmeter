#include "board.h"
#include "ch422g.h"
#include <Arduino.h>
#include <Wire.h>

// Per-board pre-init. Everything here must complete before
// display_hal_init(), because EXIO2 gates the backlight and EXIO1 gates the
// touch controller's reset.

// TEMPORARY bring-up instrumentation — removed once the port is verified on
// hardware. Prints every ACKing I2C address so the CH422G (whose 0x24/0x38/
// 0x23/0x26 respond as addresses, not as one device) and the GT911 (0x5D or
// 0x14) can be identified before a single pixel is drawn.
static void i2c_scan(void) {
    Serial.println("I2C scan:");
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) Serial.printf("  ACK 0x%02X\n", addr);
    }
    Serial.println("I2C scan done");
}

extern "C" void board_init(void) {
    Wire.begin(IIC_SDA, IIC_SCL);
    delay(50);            // let the expander and touch controller settle

    i2c_scan();           // TEMPORARY — remove once verified on hardware

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
