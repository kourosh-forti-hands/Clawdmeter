#include "../../hal/touch_hal.h"
#include "board.h"
#include "pwr_corner.h"
#include "touch_raw.h"
#include <Arduino.h>
#include <Wire.h>

// Minimal GT911 reader, vendored to keep the dependency tree copyleft-free.
// The GT911 uses 16-bit register addresses (high byte first), unlike the
// 8-bit FocalTech-style controllers on the other ports:
//   0x814E  status       — bit7 = buffer ready, low nibble = touch count
//   0x814F  track id     — point 1's id; NOT read here
//   0x8150  point 1 data — x lo, x hi, y lo, y hi, size lo, size hi
//
// Note the track id sits at 0x814F, so 0x8150 is ALREADY the x low byte.
// Reading it as though 0x8150 were the track id shifts every field one byte
// and makes the *size* field masquerade as y's high byte, which reports y in
// the thousands on a 480 px panel — verified the hard way on hardware.
//
// The status register MUST be zeroed after each read or the controller stops
// reporting new points.
//
// The board is mounted landscape with no rotation, and the GT911 reports in
// panel-native orientation, so no axis swap or mirror is applied — confirmed
// on hardware with taps at three known positions (56,60 / 410,251 / 737,447).

// Polled, NOT interrupt-driven — deliberately different from every other port.
//
// attachInterrupt() installs the GPIO ISR service by running esp_intr_alloc()
// on the ipc1 task, whose stack is ~1 KB. This board's RGB panel fires a
// bounce-buffer refill interrupt continuously, so an interrupt reliably lands
// while that call is inside heap_caps_malloc(); the CPU then pushes the
// interrupted context onto the tiny ipc stack and trips its canary:
//
//   Guru Meditation Error: Core 1 panic'ed (Unhandled debug exception)
//   Debug exception reason: Stack canary watchpoint triggered (ipc1)
//
// Polling removes the ISR install entirely. A status read is a single ~150 us
// I2C transaction, far inside the HAL's "well under 5 ms" budget, and at
// TOUCH_POLL_MS the controller is sampled faster than the panel refreshes.
#define TOUCH_POLL_MS 8

static bool     raw_pressed = false;
static uint16_t raw_x = 0;
static uint16_t raw_y = 0;
static uint32_t last_poll_ms = 0;

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
        uint8_t p[6];
        if (gt911_read(GT911_REG_POINT1, p, sizeof(p))) {
            raw_x = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
            raw_y = (uint16_t)(p[2] | ((uint16_t)p[3] << 8));
            // p[4..5] is touch size — deliberately unused.
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

    // INT is left as a plain input: the controller drives it per report, but
    // we never attach an ISR to it (see the note at the top of this file).
    pinMode(TP_INT, INPUT_PULLUP);
    Serial.printf("Touch polled every %u ms (no ISR)\n", (unsigned)TOUCH_POLL_MS);
}

void touch_raw_read(uint16_t* x, uint16_t* y, bool* pressed) {
    // Rate-limited so the two callers per loop (power_hal_tick and LVGL's
    // indev read) share one sample instead of doubling the I2C traffic.
    uint32_t now = millis();
    if ((uint32_t)(now - last_poll_ms) >= TOUCH_POLL_MS) {
        last_poll_ms = now;
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
