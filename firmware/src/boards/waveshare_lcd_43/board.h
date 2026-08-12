#pragma once

// Waveshare ESP32-S3-Touch-LCD-4.3 — 800x480 RGB parallel IPS panel.
// First parallel-bus panel in the tree: 16 data lines + DE/VSYNC/HSYNC/PCLK
// driven by the ESP32-S3 LCD peripheral out of a PSRAM framebuffer, rather
// than a serial bus feeding a panel with its own GRAM.
//
// GPIO 0 carries the green data line G3. On an ESP32-S3 GPIO 0 is also the
// BOOT strap pin, so the BOOT button is an LCD output at runtime and cannot
// be read. There is no PWR button and no secondary button — this board has
// zero readable physical buttons. See power.cpp for the touch hot corner
// that stands in for PWR.

#define BOARD_NAME           "Waveshare LCD 4.3"

// ---- Display geometry ----
#define LCD_WIDTH            800
#define LCD_HEIGHT           480

// ---- RGB panel control pins ----
#define LCD_DE               5
#define LCD_VSYNC            3
#define LCD_HSYNC            46
#define LCD_PCLK             7

// ---- RGB data pins ----
// Waveshare's documentation labels these R3-R7 / G2-G7 / B3-B7 (the upper
// bits of an 8-bit-per-channel bus). Arduino_GFX calls the same physical
// pins R0-R4 / G0-G5 / B0-B4. Same wires, different convention.
#define LCD_R0               1
#define LCD_R1               2
#define LCD_R2               42
#define LCD_R3               41
#define LCD_R4               40
#define LCD_G0               39
#define LCD_G1               0
#define LCD_G2               45
#define LCD_G3               48
#define LCD_G4               47
#define LCD_G5               21
#define LCD_B0               14
#define LCD_B1               38
#define LCD_B2               18
#define LCD_B3               17
#define LCD_B4               10

// ---- Panel timings ----
#define LCD_HSYNC_POLARITY   0
#define LCD_HSYNC_FRONT      40
#define LCD_HSYNC_PULSE      48
#define LCD_HSYNC_BACK       88
#define LCD_VSYNC_POLARITY   0
#define LCD_VSYNC_FRONT      13
#define LCD_VSYNC_PULSE      3
#define LCD_VSYNC_BACK       32
#define LCD_PCLK_ACTIVE_NEG  1
#define LCD_PREFER_SPEED     16000000
// Bounce buffers: REQUIRED on this board.
//
// With the framebuffer in PSRAM, the RGB peripheral's scan-out DMA competes
// with the CPU for PSRAM bandwidth and starves mid-frame. On hardware at 0
// this shows as heavy flicker during the splash animation. Routing the scan
// through two internal-SRAM bounce buffers (PSRAM -> SRAM -> LCD) decouples
// the panel's timing from that contention and the animation becomes smooth —
// verified by A/B on real hardware, 0 vs the value below.
//
// 10 lines * 800 px = 16 KB per buffer, 32 KB of internal SRAM total.
// 20 lines, not 10: artifacts appeared along the LEFT edge, which is the
// signature of the bounce buffer being refilled too late — the peripheral
// reads the start of each scanline before the DMA has delivered it. More
// headroom per refill is the first remedy; lowering LCD_PREFER_SPEED is
// the second.
#define LCD_BOUNCE_BUF_PX    (LCD_WIDTH * 20)

// ---- I2C bus (CH422G expander + GT911 touch) ----
#define IIC_SDA              8
#define IIC_SCL              9

// ---- CH422G IO expander ----
// No register pointer: each I2C address IS a register. Values are the
// datasheet's 8-bit addresses >> 1. Confirmed against Espressif's
// ESP32_IO_Expander and ESPHome's ch422g component, which agree exactly.
#define CH422G_WR_SET        0x24   // mode/config; bit0 = IO_OE
#define CH422G_WR_IO         0x38   // outputs IO0..IO7 (bit n = EXIO n)
#define CH422G_WR_OC         0x23   // open-drain outputs OC0..OC3
#define CH422G_RD_IO         0x26   // input read
#define CH422G_MODE_IO_OE    0x01

#define EXIO_TP_RST          1      // touch reset
#define EXIO_LCD_BL          2      // backlight / DISP enable

// ---- Touch (GT911) ----
// Address is selected by the INT level as reset is released: INT low => 0x5D.
// WR_IO powers up at 0xFF, so the controller boots OUT of reset — the reset
// pulse must actively drive EXIO1 low, not merely release it.
#define TP_INT               4
#define GT911_ADDR           0x5D
#define GT911_ADDR_ALT       0x14
#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_STATUS     0x814E
// 0x814F is point 1's track id; 0x8150 is already the x LOW byte, so the
// record read from here is x_lo, x_hi, y_lo, y_hi, size_lo, size_hi.
#define GT911_REG_POINT1     0x8150

// ---- Virtual PWR button ----
// No physical button exists. power.cpp synthesises PWR press/long-press/
// release from a square hot corner in the bottom-left, and touch.cpp hides
// that region from LVGL so it does not also fire the splash toggle.
#define PWR_CORNER_PX        72

// ---- Capability flags ----
#define BOARD_HAS_SECONDARY_BUTTON 0
#define BOARD_HAS_ROTATION         0
#define BOARD_HAS_IMU              0
#define BOARD_HAS_BATTERY          0   // PH2.0 connector, but no voltage ADC
#define BOARD_HAS_IO_EXPANDER      1
#define BOARD_HAS_SOUND            0
