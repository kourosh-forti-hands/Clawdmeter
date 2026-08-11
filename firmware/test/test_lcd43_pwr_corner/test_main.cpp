// Host unit test for the virtual PWR button's hit region. The 4.3 has no
// readable physical button, so a square of glass stands in for one — getting
// this region wrong either steals taps from the UI or makes PWR unreachable.
//
//   g++ -std=c++17 -I ../../src test_main.cpp -o t && ./t

#include "boards/waveshare_lcd_43/pwr_corner.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void should_accept_the_bottom_left_corner() {
    CHECK(pwr_corner_contains(0, 479, 480));
    CHECK(pwr_corner_contains(71, 408, 480));
    CHECK(pwr_corner_contains(0, 408, 480));
}

static void should_reject_just_outside_the_corner() {
    CHECK(!pwr_corner_contains(72, 479, 480));   // one px right of the edge
    CHECK(!pwr_corner_contains(71, 407, 480));   // one px above the edge
}

static void should_reject_the_rest_of_the_screen() {
    CHECK(!pwr_corner_contains(400, 240, 480));  // centre
    CHECK(!pwr_corner_contains(0, 0, 480));      // top-left
    CHECK(!pwr_corner_contains(799, 479, 480));  // bottom-right
}

int main() {
    should_accept_the_bottom_left_corner();
    should_reject_just_outside_the_corner();
    should_reject_the_rest_of_the_screen();
    if (failures == 0) printf("all pwr_corner tests passed\n");
    return failures != 0;
}
