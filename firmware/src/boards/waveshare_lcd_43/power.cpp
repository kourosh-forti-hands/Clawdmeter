#include "../../hal/power_hal.h"

// No PMU and no battery ADC on this board. The PH2.0 connector is wired to a
// CS8501 charger with no voltage divider back to the MCU, so battery state is
// unknowable — BOARD_HAS_BATTERY is 0 and the UI hides the indicator.
//
// Task 7 replaces the PWR stubs with a virtual button driven by a touch hot
// corner, which is what makes the hold-to-pair gesture in main.cpp reachable.

void power_hal_init(void) {}
void power_hal_tick(void) {}

int  power_hal_battery_pct(void) { return -1; }
bool power_hal_is_charging(void) { return false; }
bool power_hal_is_vbus_in(void)  { return true; }   // USB-powered by definition

bool power_hal_pwr_pressed(void)      { return false; }
bool power_hal_pwr_long_pressed(void) { return false; }
bool power_hal_pwr_released(void)     { return false; }
