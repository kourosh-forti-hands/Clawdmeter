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

static void should_accept_the_top_right_corner() {
    CHECK(pwr_corner_contains(799, 0, 800, 480));
    CHECK(pwr_corner_contains(728, 71, 800, 480));
    CHECK(pwr_corner_contains(799, 71, 800, 480));
}

static void should_reject_just_outside_the_corner() {
    CHECK(!pwr_corner_contains(727, 0, 800, 480));    // one px left of the edge
    CHECK(!pwr_corner_contains(728, 72, 800, 480));   // one px below the edge
}

static void should_reject_the_rest_of_the_screen() {
    CHECK(!pwr_corner_contains(400, 240, 800, 480));  // centre
    CHECK(!pwr_corner_contains(0, 0, 800, 480));      // top-left
    CHECK(!pwr_corner_contains(799, 479, 800, 480));  // bottom-right
}

// The five-key deck spans the full width at the bottom; the corner must not
// steal any of it. TALK is the leftmost key, roughly x 21..163, y 398..458.
static void should_not_overlap_the_bottom_key_deck() {
    for (int16_t x = 0; x < 800; x += 7)
        for (int16_t y = 390; y < 480; y += 5)
            CHECK(!pwr_corner_contains(x, y, 800, 480));
}

int main() {
    should_accept_the_top_right_corner();
    should_not_overlap_the_bottom_key_deck();
    should_reject_just_outside_the_corner();
    should_reject_the_rest_of_the_screen();
    if (failures == 0) printf("all pwr_corner tests passed\n");
    return failures != 0;
}
