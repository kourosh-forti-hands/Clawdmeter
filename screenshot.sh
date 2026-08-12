#!/bin/bash
# Take a screenshot from the display via LVGL snapshot.
# Usage: ./screenshot.sh [output.png] [port] [screen]
# Default port: /dev/cu.usbmodem101 on macOS, /dev/ttyACM0 on Linux.
#
# [screen] is optional and selects a page before capturing, using the
# firmware's "screen N" serial command: 0 splash, 1 usage, 2 detail,
# 3 system, 4 activity. Pass it whenever you want a specific page — opening
# the serial port resets the board on most USB-serial bridges, and a board
# that has just reset is sitting on the splash, so without it you will
# usually photograph the splash no matter what was on screen before.

OUTPUT="${1:-screenshot.png}"
if [ -z "$2" ]; then
    case "$(uname -s)" in
        Darwin) PORT="/dev/cu.usbmodem101" ;;
        *)      PORT="/dev/ttyACM0" ;;
    esac
else
    PORT="$2"
fi
SCREEN="${3:-}"

# Use pio's bundled python if pyserial isn't on the system python.
PY="python3"
if ! python3 -c "import serial" 2>/dev/null; then
    if [ -x "$HOME/.platformio/penv/bin/python" ]; then
        PY="$HOME/.platformio/penv/bin/python"
    fi
fi

TMPRAW=$(mktemp /tmp/screenshot_XXXXXX.raw)
TMPDIMS=$(mktemp /tmp/screenshot_XXXXXX.dims)
trap "rm -f '$TMPRAW' '$TMPDIMS'" EXIT

echo "Taking screenshot from $PORT..."

"$PY" - "$PORT" "$TMPRAW" "$TMPDIMS" "$SCREEN" << 'PYEOF'
import serial, sys, time

port_path, raw_path, dims_path = sys.argv[1], sys.argv[2], sys.argv[3]
screen = sys.argv[4] if len(sys.argv) > 4 else ""

# Open WITHOUT asserting DTR/RTS. USB-serial bridges wire those two lines to
# the ESP32's EN and BOOT pins for auto-reset, and pyserial raises both by
# default at open — which can leave the chip held in reset, so the capture
# hangs. Assigning them before open() applies the levels as the port comes
# up, rather than pulsing them and then dropping them.
#
# This reduces the problem but does not always eliminate it: some bridge and
# driver combinations still reset the board on open (observed on a CH343
# under macOS's generic CDC-ACM driver, which reports rst:0x1 POWERON). So
# the code below waits for the firmware to finish booting rather than writing
# into a chip that is still in the ROM loader — and that is also why the
# optional [screen] argument exists, since a board that just reset is showing
# the splash.
port = serial.Serial()
port.port = port_path
port.baudrate = 115200
port.timeout = 2
port.dtr = False
port.rts = False
port.open()

# Settle: either the board boots (we see the ready banner) or it was already
# running and simply has nothing to say, which shows up as a quiet port.
deadline, quiet_since = time.time() + 15, None
buf = b""
while time.time() < deadline:
    chunk = port.read(512)
    if chunk:
        buf += chunk
        quiet_since = None
        if b"Dashboard ready" in buf:
            break
    else:
        if quiet_since is None:
            quiet_since = time.time()
        elif time.time() - quiet_since > 1.0:
            break   # already up and idle

if screen:
    port.write(f"screen {int(screen)}\n".encode())
    port.flush()
    time.sleep(1.2)   # let the page build and render before snapshotting

# Bounded, and retried: an unbounded readline loop hangs forever if the
# header never arrives (readline returns "" on timeout and the loop spins).
w = h = raw_size = None
for attempt in range(3):
    port.reset_input_buffer()
    port.write(b"screenshot\n")
    port.flush()
    attempt_deadline = time.time() + 20
    while time.time() < attempt_deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("SCREENSHOT_START"):
            parts = line.split()
            w, h, raw_size = int(parts[1]), int(parts[2]), int(parts[3])
            break
        if line == "SCREENSHOT_ERR":
            print("Device reported screenshot error", file=sys.stderr)
            sys.exit(1)
        if line == "SCREENSHOT_UNSUPPORTED":
            print("This board cannot capture (no PSRAM; LV_USE_SNAPSHOT=0)",
                  file=sys.stderr)
            sys.exit(1)
    if w is not None:
        break

if w is None:
    print("No response from device after 3 attempts — is it running this "
          "firmware, and is anything else holding the port?", file=sys.stderr)
    sys.exit(1)

# Roomier timeout for the bulk transfer: 768 KB at 115200 baud is ~66 s of
# continuous data, and a short per-read timeout would abort on any hiccup.
port.timeout = 10

data = b""
while len(data) < raw_size:
    chunk = port.read(min(4096, raw_size - len(data)))
    if not chunk:
        print(f"Timeout: got {len(data)} of {raw_size} bytes", file=sys.stderr)
        sys.exit(1)
    data += chunk

with open(raw_path, "wb") as f:
    f.write(data)
with open(dims_path, "w") as f:
    f.write(f"{w}x{h}\n")

for _ in range(10):
    line = port.readline().decode("utf-8", errors="replace").strip()
    if line == "SCREENSHOT_END":
        break

port.close()
print(f"Captured {w}x{h} ({len(data)} bytes)")
PYEOF

if [ $? -ne 0 ]; then
    echo "Screenshot capture failed"
    exit 1
fi

DIMS=$(cat "$TMPDIMS")
ffmpeg -y -f rawvideo -pixel_format rgb565le -video_size "$DIMS" \
    -i "$TMPRAW" -update 1 -frames:v 1 "$OUTPUT" 2>/dev/null || true


if [ -f "$OUTPUT" ]; then
    echo "Saved: $OUTPUT ($DIMS)"
else
    echo "Error: conversion failed"
    exit 1
fi
