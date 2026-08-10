# Design: Waveshare ESP32-S3-Touch-LCD-4.3 port

**Date:** 2026-08-10
**Status:** Approved, ready for implementation planning
**Build env:** `waveshare_lcd_43`

## Goal

Add a seventh board port for the Waveshare ESP32-S3-Touch-LCD-4.3 — an 800×480
RGB parallel IPS panel with GT911 touch and a CH422G IO expander. This is the
first **parallel-bus** panel in the tree (all six existing ports drive a serial
bus: QSPI on the AMOLEDs, 4-wire SPI on the LCD-1.54) and the first board with
**no readable physical button**.

The port must follow the existing contract: a new folder under
`firmware/src/boards/` plus a new `[env:...]` block, with no `#ifdef BOARD_*`
in shared code.

## Why this board is different

An RGB parallel panel has no frame memory of its own. The ESP32-S3 LCD
peripheral continuously re-scans a 768 KB framebuffer in PSRAM at the panel's
refresh rate. `display_hal_draw_bitmap()` therefore becomes a copy into RAM
rather than a bus transaction, and the panel is "always drawing" whether or not
we push anything.

The board also spends 20 GPIOs on the RGB bus. Of note, **GPIO 0 carries the
green data line G3** — and on an ESP32-S3 GPIO 0 *is* the BOOT strap pin, so the
BOOT button is an RGB output at runtime and cannot be read. There is no PWR
button and no secondary button. The board therefore has **zero usable physical
buttons**.

## Verified hardware

Pin map cross-checked between Waveshare's documentation and a hardware-tested
Arduino_GFX configuration. Waveshare labels the data lines R3–R7 / G2–G7 /
B3–B7 (the upper bits of an 8-bit-per-channel bus) while Arduino_GFX calls the
same physical pins R0–R4 / G0–G5 / B0–B4. Same wires, different convention —
mixing the two conventions produces a scrambled or colour-shifted image.

| Signal | GPIO |
|---|---|
| DE / VSYNC / HSYNC / PCLK | 5 / 3 / 46 / 7 |
| R0–R4 | 1, 2, 42, 41, 40 |
| G0–G5 | 39, 0, 45, 48, 47, 21 |
| B0–B4 | 14, 38, 18, 17, 10 |
| I2C SDA / SCL | 8 / 9 |
| Touch INT | 4 |
| USB D− / D+ | 19 / 20 |

Panel timing: hsync polarity 0, front porch 40, pulse width 48, back porch 88;
vsync polarity 0, front porch 13, pulse width 3, back porch 32;
`pclk_active_neg = 1`, pixel clock 16 MHz.

Module: ESP32-S3R8 — 16 MB quad flash, 8 MB octal PSRAM.

**CH422G IO expander.** An unusual part: it has no register pointer, so each
I²C *address* is itself a register.

| Address | Purpose |
|---|---|
| `0x24` | mode/config (`0x01` = outputs on pins 0–7, `0x04` = open-drain on 8–11) |
| `0x38` | write outputs 0–7 |
| `0x23` | write open-drain outputs 8–11 |
| `0x26` | read inputs |

Expander pin assignments: **EXIO1** = touch reset, **EXIO2** = backlight /
`DISP` enable, **EXIO4** = SD card CS, **EXIO5** = USB/CAN select.

**Absent hardware:** no PMU, no battery voltage ADC (the PH2.0 connector is
charge-only from the firmware's perspective), no IMU, no audio codec.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Variant | Plain 4.3 (not 4.3B) | Confirmed by the user; the 4.3B has a different layout. |
| HID buttons | On-screen soft TALK / MODE buttons | Keeps the headline push-to-talk feature on a board with no physical buttons; 800×480 has room to spare. |
| Usage-screen layout | Two-column landscape | First landscape port; a centred 480 px column would waste 40% of the panel. |
| Brightness / idle | Per-pixel dim + hard backlight cut | The backlight is a single on/off line, so graduated brightness has to be synthesised in the pixel path. |

## Architecture

### Display path

**Keep LVGL in `LV_DISPLAY_RENDER_MODE_PARTIAL` and copy strips into the RGB
framebuffer.** `display_hal_draw_bitmap()` calls
`gfx->draw16bitRGBBitmap(x, y, pixels, w, h)` exactly as the LCD-1.54 port
does; the only difference is that the destination is PSRAM the LCD peripheral
is scanning. This requires **no shared-code changes**.

Cost: one extra copy per flush — 800 × 40 × 2 = 64 KB at roughly 40 MB/s PSRAM
write bandwidth, about 1.6 ms, comfortably inside LVGL's render budget.

Rejected alternatives:

- **DIRECT render mode**, with LVGL drawing straight into the panel
  framebuffer. Zero-copy and faster, but it requires `main.cpp` to call
  `lv_display_set_buffers(..., LV_DISPLAY_RENDER_MODE_DIRECT)` with a pointer
  the board owns, which means a new HAL entry point and an edit to shared code
  — breaking the porting contract for a latency win this workload (roughly 1 Hz
  data updates) does not need.
- **Double-buffered page flipping.** Best tearing behaviour, but another 768 KB
  of PSRAM and a larger HAL change.

If tearing appears on hardware, the first remedy is
`Arduino_ESP32RGBPanel`'s `bounce_buffer_size_px` argument, not a redesign.

### Memory budget (8 MB PSRAM)

| Allocation | Size |
|---|---|
| RGB framebuffer (800 × 480 × 2) | 768 KB |
| LVGL buffers (2 × 800 × 40 × 2) | 128 KB |
| Splash canvas + mini canvas | ~460 KB |
| Screenshot buffer (transient, `LV_USE_SNAPSHOT`) | 768 KB |

Comfortable. `LV_USE_SNAPSHOT` stays enabled, so `./screenshot.sh` works and
UI iteration does not require eyeballing hardware.

### Brightness and idle

The backlight is a single on/off line on the expander, so
`display_hal_set_brightness(level)` is synthesised in two parts:

1. **Level < 8: drive EXIO2 low.** The panel goes genuinely dark and draws no
   backlight power. This is what the idle timeout ends in, and touch wakes it.
2. **Level >= 8: scale pixels on their way to the
   framebuffer**, with EXIO2 held high. `display_hal_draw_bitmap()` applies a per-channel lookup
   table (`r5[32]`, `g6[64]`, `b5[32]`, rebuilt only when the level changes)
   while copying. At full brightness the LUT path is skipped entirely and the
   plain fast copy runs.

**Why dim in `draw_bitmap` rather than with an LVGL overlay:** `splash.cpp:126`
pushes the splash animation straight to the panel via `display_hal_draw_bitmap`,
bypassing LVGL completely. An LVGL top-layer scrim would be painted over by the
splash every frame, giving dimmed chrome and a full-brightness creature. It
would also have an initialisation-order hazard — `display_hal_begin()` runs
*before* `lv_init()` in `main.cpp::setup()`. `display_hal_draw_bitmap` is the
single function every pixel passes through, so dimming there covers both paths
with no LVGL dependency.

Because the framebuffer retains whatever brightness a pixel was written at, a
level change must be followed by a repaint. `display_hal_tick()` — already
called every loop from `main.cpp` and already used by the 2.16 port for its
rotation-transition redraw — detects a pending level change and calls
`lv_obj_invalidate(lv_screen_active())`.

**Known limitation with a defined fallback:** while the splash is showing, only
the cells the animation touches get repainted, so a brightness change could
leave transient banding until the creature moves. In practice the window is
narrow — `main.cpp:346` routes the user-initiated brightness cycle to
`brightness_cycle()` only when *not* on the splash screen, and the idle path
terminates in a hard backlight cut. If banding is visible on hardware, the
fallback is to force full brightness whenever `splash_is_active()` returns true.

### The virtual PWR button

`power_hal.h` defines `power_hal_pwr_pressed()` / `_long_pressed()` /
`_released()` as edge-triggered events and deliberately does not say where the
press comes from — the 2.16 sources them from a PMU IRQ, the 1.8 from an IO
expander poll. Nothing requires a button.

The 4.3 synthesises them from a **72 × 72 px hot corner in the bottom-left**
(`x < 72 && y >= height - 72`), driven by the board's own touch state inside
`power.cpp`:

- tap → `power_hal_pwr_pressed()`
- held ≥ 1.5 s → `power_hal_pwr_long_pressed()`
- release → `power_hal_pwr_released()`

This means the hold-to-pair gesture in `main.cpp:257`, the brightness cycle and
the splash-animation cycle all work **without any shared-code change of their
own** — the two `ui.cpp` edits below are for layout and soft HID buttons only.

To stop the corner from also firing LVGL's `global_click_cb` (which toggles
splash ↔ usage), `touch_hal_read()` reports `pressed = false` while the finger
is inside the corner. LVGL never sees it. It is a physical button that happens
to be made of glass.

### New board folder

```
firmware/src/boards/waveshare_lcd_43/
  board.h          20 RGB pins + timings, I2C 8/9, TP_INT 4, addresses, BOARD_HAS_* flags
  ch422g.{h,cpp}   vendored expander driver (~50 lines)
  board_init.cpp   Wire.begin(8,9) → ch422g_init() → backlight on, touch reset pulse
  display.cpp      Arduino_ESP32RGBPanel + Arduino_RGB_Display, LUT dimming, EXIO2 blank
  touch.cpp        vendored GT911 reader + hot-corner suppression
  input.cpp        both buttons return false (no readable GPIO)
  power.cpp        battery -1, charging false; PWR synthesised from the hot corner
  imu.cpp          stubs, rotation quadrant always 0
  sound.cpp        no-op (no codec on this board)
  caps.cpp         button_count = 0; has_battery / has_rotation / has_imu all false
```

Both the CH422G and GT911 drivers are vendored rather than pulled from a
library, matching the existing FT3168 and CST816T readers and keeping the
dependency tree free of copyleft.

**GT911 addressing.** The controller's I²C address is selected by the INT pin
level as reset is released. Because the port owns both TP_RST (EXIO1) and INT
(GPIO 4), the address is pinned deterministically rather than probed: hold INT
low across the reset release to select **0x5D**. Registers used: `0x8140`
(product ID, for the boot-time log), `0x814E` (status / touch count), `0x8150`
(point data). Write 0 back to `0x814E` after each read or the controller stops
reporting.

## Shared-code changes

Exactly two, both in `ui.cpp`, both gated on runtime capabilities rather than
board identity:

1. **`compute_layout()`** — a new branch keyed on `width >= 700 && width >
   height` producing the two-column landscape layout. Purely additive; the
   three existing breakpoints are untouched.
2. **Soft HID buttons** — TALK and MODE controls built only when
   `board_caps().button_count == 0`. They call the same
   `ble_keyboard_press(0x2C, 0)` / `ble_keyboard_release()` (Space, held for
   push-to-talk) and `ble_keyboard_press(0x2B, 0x02)` (Shift+Tab) that
   `main.cpp` calls for physical buttons. `ui.h` already includes `ble.h`, so no
   new coupling is introduced.

`button_count == 0` is a new *value* for an existing `BoardCaps` field, not a
new field. `main.cpp:326` already gates the secondary-button HID on
`button_count >= 2`, so this extends a pattern that exists.

The soft buttons appear on the usage screen only. The splash screen keeps its
full-screen tap-to-return behaviour.

## Build configuration

Summary of the new env block (not literal INI):

```text
[env:waveshare_lcd_43]
platform  = pioarduino 55.03.38-1   ; as every other env
board     = esp32-s3-devkitc-1
board_build.arduino.memory_type = qio_opi     ; octal PSRAM, quad flash
board_upload.flash_size   = 16MB
board_build.partitions    = default_16MB.csv
build_flags: -DBOARD_LCD_43, -DBOARD_HAS_PSRAM, -DARDUINO_USB_CDC_ON_BOOT=1,
             LVGL flags as the LCD-1.54 env, -DLV_USE_SNAPSHOT=1
lib_deps:    GFX Library for Arduino ^1.6.4, lvgl ^9.2.0, ArduinoJson ^7,
             NimBLE-Arduino ^2.1.1
```

No SensorLib and no XPowersLib — there is no IMU and no PMU on this board.

## Bring-up order and failure modes

`board_init()` must complete this sequence **before** `display_hal_init()`:

1. `Wire.begin(8, 9)`
2. `ch422g_init()` — configure `0x24` for push-pull outputs
3. drive EXIO2 high (backlight enable)
4. pulse EXIO1 low → high (touch reset) with INT held low to pin the address

Getting this wrong reproduces gotcha #9 from `CLAUDE.md`: a dark panel and a
silent touch controller, with no panic to point at the cause. Every init step
logs OK/failure over serial, matching the other ports.

`display_hal_begin()` checks the framebuffer allocation and logs loudly on
failure — the RGB peripheral will happily scan an unallocated or garbage buffer
and show noise rather than crash.

## Testing

- **Build** is the first real check: the link step catches any missing or
  duplicated HAL symbol. `pio run -d firmware -e waveshare_lcd_43`.
- **Existing envs must still build** — the two `ui.cpp` changes are shared, so
  at minimum `waveshare_amoled_216` (large layout, 2 buttons) and
  `waveshare_lcd_154` (small layout, battery) get a regression build.
- **Smoke test on hardware:** splash renders, tap toggles to the usage screen,
  hot corner cycles brightness, BLE advertises and the macOS daemon connects.
- **Visual QA via `./screenshot.sh`**, iterating the two-column layout from
  captured PNGs rather than from photographs of the panel. Per `CLAUDE.md`,
  temporarily boot to `SCREEN_USAGE` during iteration and revert before
  committing.
- **Host side:** macOS is already supported through `install-mac.sh`
  (Python + `bleak` + launchd). No daemon work is in scope.

## Out of scope

- SD card, CAN and RS485 peripherals — present on the board, irrelevant to a
  usage monitor.
- Battery percentage reporting — no ADC path exists; `has_battery = false`
  hides the indicator.
- Audio / chime — no codec on this board; `sound.cpp` no-ops as it does on the
  AMOLED-2.06 and C6 1.8 ports.
- Rotation — fixed landscape orientation, no IMU.

## To verify at first bring-up

These are known-uncertain and each has a defined next action, rather than being
open questions blocking the design:

1. **CH422G EXIO numbering.** Whether EXIO1/EXIO2 map to output bits 1 and 2 of
   register `0x38` or are offset by one. Action: at bring-up, walk the output
   bits one at a time and observe which toggles the backlight.
2. **GT911 address selection timing.** If 0x5D does not ACK, retry with INT
   held high (address 0x14) before assuming a wiring fault.
3. **Tearing.** If visible, add `bounce_buffer_size_px` to the
   `Arduino_ESP32RGBPanel` constructor.
4. **Backlight current draw.** If the board brownouts or reset-loops during
   bring-up on a laptop USB port, retry on a 1 A+ supply before suspecting
   firmware.

## Documentation to update on completion

- `CLAUDE.md` — board inventory, critical pins, architecture tree, build
  commands.
- `docs/porting/adding-a-board.md` — the "Hardware you need" section still says
  a QSPI AMOLED panel is required and that other interfaces are unsupported.
  The LCD-1.54 port already made that half-false; this port makes it fully
  false.
- `docs/porting/hal-contract.md` — record the new landscape breakpoint.
