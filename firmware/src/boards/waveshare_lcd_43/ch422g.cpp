#include "ch422g.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>

// WR_IO powers up at 0xFF (all outputs HIGH). Seeding the cache with the
// hardware default matters: the GT911 therefore boots OUT of reset, and a
// reset pulse must actively drive EXIO1 low rather than merely releasing it.
static uint8_t io_state = 0xFF;

bool ch422g_write_raw(uint8_t reg_addr, uint8_t value) {
    Wire.beginTransmission(reg_addr);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool ch422g_init(void) {
    // WR_SET defaults to 0x01 (IO_OE already set), so this write is
    // belt-and-braces rather than load-bearing — but it makes the state
    // explicit after a warm reset that left the chip in some other mode.
    if (!ch422g_write_raw(CH422G_WR_SET, CH422G_MODE_IO_OE)) {
        Serial.println("CH422G: no ACK on WR_SET (0x24) — expander missing?");
        return false;
    }
    if (!ch422g_write_raw(CH422G_WR_IO, io_state)) {
        Serial.println("CH422G: no ACK on WR_IO (0x38)");
        return false;
    }
    Serial.printf("CH422G OK (WR_IO=0x%02X)\n", io_state);
    return true;
}

void ch422g_set(uint8_t exio, bool high) {
    if (exio > 7) return;
    if (high) io_state |= (uint8_t)(1u << exio);
    else      io_state &= (uint8_t)~(1u << exio);
    ch422g_write_raw(CH422G_WR_IO, io_state);
}

uint8_t ch422g_io_state(void) { return io_state; }
