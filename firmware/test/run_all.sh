#!/bin/bash
# Run every host unit test.
#
# These tests compile the pure, hardware-free parts of the firmware on the
# development machine — no board required. Each test directory holds one
# test_main.cpp that includes its subject directly (the subjects keep their
# state in file statics with no accessors, so sharing a translation unit is
# the only way to drive them off-target).
#
# Both include paths matter and neither is optional:
#   -I ../../src  finds the subject under test
#   -I .          finds any test-local shim, e.g. test_usage_rate/Arduino.h,
#                 which provides a fake-clock millis() for code that calls it.
#                 Angle-bracket includes resolve only via -I paths, never via
#                 the including file's own directory, so without this the
#                 usage-rate test fails with "Arduino.h file not found".
#
# Usage: firmware/test/run_all.sh   (exits non-zero if any test fails)

set -u
cd "$(dirname "$0")"

fails=0
for dir in */; do
    d="${dir%/}"
    [ -f "$d/test_main.cpp" ] || continue
    printf '%-26s ' "$d"
    if ! out=$( cd "$d" && g++ -std=c++17 -I . -I ../../src test_main.cpp -o t 2>&1 ); then
        echo "BUILD FAILED"
        echo "$out" | head -5 | sed 's/^/    /'
        fails=$((fails + 1))
        continue
    fi
    if out=$( cd "$d" && ./t 2>&1 ); then
        echo "$out"
    else
        echo "FAILED"
        echo "$out" | head -10 | sed 's/^/    /'
        fails=$((fails + 1))
    fi
done

echo
if [ "$fails" -eq 0 ]; then
    echo "all host tests passed"
else
    echo "$fails test(s) failed"
fi
exit "$fails"
