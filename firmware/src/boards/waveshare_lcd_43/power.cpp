#include "../../hal/power_hal.h"
#include "board.h"
#include "pwr_corner.h"
#include "touch_raw.h"
#include <Arduino.h>

// No PMU and no battery ADC on this board. The PH2.0 connector is wired to a
// CS8501 charger with no voltage divider back to the MCU, so battery state is
// unknowable — BOARD_HAS_BATTERY is 0 and the UI hides the indicator.
//
// The PWR button is synthesised from a square of glass in the bottom-left
// corner. power_hal's contract deliberately does not say where a press comes
// from — the 2.16 sources it from a PMU IRQ, the 1.8 from an IO expander poll
// — so a touch region is a legitimate source, and the hold-to-pair gesture in
// main.cpp works unmodified.

#define PWR_LONG_MS 1500   // matches the ~1.5s threshold power_hal documents

static bool     corner_held    = false;
static uint32_t corner_down_ms = 0;
static bool     long_fired     = false;

static bool ev_pressed  = false;   // edge flags, drained by the getters
static bool ev_long     = false;
static bool ev_released = false;

void power_hal_init(void) {
    Serial.println("Power: no PMU; PWR is the bottom-left touch corner");
}

void power_hal_tick(void) {
    uint16_t x = 0, y = 0;
    bool pressed = false;
    touch_raw_read(&x, &y, &pressed);

    bool in_corner = pressed &&
                     pwr_corner_contains((int16_t)x, (int16_t)y, LCD_WIDTH, LCD_HEIGHT);

    if (in_corner && !corner_held) {
        corner_held = true;
        corner_down_ms = millis();
        long_fired = false;
    } else if (!in_corner && corner_held) {
        corner_held = false;
        // A short tap is the "press" event; a long hold has already fired its
        // own event and its release completes the pairing gesture instead.
        if (!long_fired) ev_pressed = true;
        ev_released = true;
    } else if (corner_held && !long_fired &&
               (millis() - corner_down_ms) >= PWR_LONG_MS) {
        long_fired = true;
        ev_long = true;
    }
}

int  power_hal_battery_pct(void) { return -1; }
bool power_hal_is_charging(void) { return false; }
bool power_hal_is_vbus_in(void)  { return true; }

bool power_hal_pwr_pressed(void) {
    bool v = ev_pressed;
    ev_pressed = false;
    return v;
}

bool power_hal_pwr_long_pressed(void) {
    bool v = ev_long;
    ev_long = false;
    return v;
}

bool power_hal_pwr_released(void) {
    bool v = ev_released;
    ev_released = false;
    return v;
}
