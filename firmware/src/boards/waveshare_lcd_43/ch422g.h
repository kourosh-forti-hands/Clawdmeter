#pragma once
#include <stdint.h>

// Minimal CH422G IO expander driver, vendored to keep the dependency tree
// copyleft-free and to avoid pulling in ESP_IOExpander for four register
// writes.
//
// The chip has no register pointer: each I2C *address* is itself a register
// (see the CH422G_* defines in board.h). A write is therefore a one-byte
// transaction to the register's own address, not the usual
// address-then-register-then-value.
//
// Register map confirmed against two independent implementations that agree
// exactly: Espressif's ESP32_IO_Expander and ESPHome's ch422g component.

// Probe and configure IO0-7 as outputs. Returns false if the chip does not
// ACK. Safe to call once from board_init().
bool ch422g_init(void);

// Drive EXIO n (0..7) high or low, preserving the other bits.
void ch422g_set(uint8_t exio, bool high);

// Escape hatch for bring-up instrumentation and OC-pin access.
bool ch422g_write_raw(uint8_t reg_addr, uint8_t value);

// Current cached output byte (what was last written to WR_IO).
uint8_t ch422g_io_state(void);
