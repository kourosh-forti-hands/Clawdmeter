#include "../../hal/touch_hal.h"
#include "board.h"
#include "pwr_corner.h"
#include "touch_raw.h"
#include <Arduino.h>
#include <Wire.h>

// Minimal GT911 reader, vendored to keep the dependency tree copyleft-free.
// The GT911 uses 16-bit register addresses (high byte first), unlike the
// 8-bit FocalTech-style controllers on the other ports:
//   0x814E  status  — bit7 = buffer ready, low nibble = touch count
//   0x8150  point 1 — track id, x lo, x hi, y lo, y hi, size lo, size hi, rsv
// The status register MUST be zeroed after each read or the controller stops
// reporting new points.
//
// The board is mounted landscape with no rotation, and the GT911 reports in
// panel-native orientation, so no axis swap or mirror is applied.

static volatile bool touch_data_ready = false;
static bool     raw_pressed = false;
static uint16_t raw_x = 0;
static uint16_t raw_y = 0;

static void IRAM_ATTR touch_isr(void) {
    touch_data_ready = true;
}

static bool gt911_read(uint16_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)GT911_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
    return true;
}

static bool gt911_write_u8(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(GT911_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static void touch_pump(void) {
    uint8_t status = 0;
    if (!gt911_read(GT911_REG_STATUS, &status, 1)) { raw_pressed = false; return; }
    if ((status & 0x80) == 0) return;        // no new data; keep the last sample

    uint8_t count = status & 0x0F;
    if (count == 0 || count > 5) {
        raw_pressed = false;
    } else {
        uint8_t p[8];
        if (gt911_read(GT911_REG_POINT1, p, sizeof(p))) {
            raw_x = (uint16_t)(p[1] | ((uint16_t)p[2] << 8));
            raw_y = (uint16_t)(p[3] | ((uint16_t)p[4] << 8));
            raw_pressed = true;
        }
    }
    gt911_write_u8(GT911_REG_STATUS, 0);     // required, or reporting stalls
}

void touch_hal_init(void) {
    // The reset pulse and address selection already happened in board_init(),
    // which must run before this. Confirm the controller answers.
    uint8_t id[4] = {0, 0, 0, 0};
    if (gt911_read(GT911_REG_PRODUCT_ID, id, sizeof(id))) {
        Serial.printf("Touch GT911 ID=%c%c%c%c (addr 0x%02X)\n",
                      id[0], id[1], id[2], id[3], GT911_ADDR);
    } else {
        Serial.printf("Touch GT911 ID read FAILED (addr 0x%02X)\n", GT911_ADDR);
    }

    pinMode(TP_INT, INPUT_PULLUP);
    attachInterrupt(TP_INT, touch_isr, FALLING);
    Serial.println("Touch attached on INT pin");
}

void touch_raw_read(uint16_t* x, uint16_t* y, bool* pressed) {
    if (touch_data_ready) {
        touch_data_ready = false;
        touch_pump();
    } else if (raw_pressed) {
        // The finger-up report can land between polls; re-read while we think
        // we are pressed so a stuck "pressed" state clears.
        touch_pump();
    }
    *x = raw_x;
    *y = raw_y;
    *pressed = raw_pressed;
}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    touch_raw_read(x, y, pressed);

    // Hide the virtual PWR button's corner from LVGL. Without this a PWR tap
    // would also reach ui.cpp's global_click_cb and toggle the splash screen.
    if (*pressed && pwr_corner_contains((int16_t)*x, (int16_t)*y, LCD_HEIGHT)) {
        *pressed = false;
    }
}
