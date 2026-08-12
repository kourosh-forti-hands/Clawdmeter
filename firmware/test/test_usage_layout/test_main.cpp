// Host unit test for usage-panel slot geometry. Every board stacks its two
// panels at full content width — the canonical Clawdmeter layout. These cases
// pin the geometry of all existing boards so a layout change cannot silently
// reflow them.
//
//   g++ -std=c++17 -I . -I ../../src test_main.cpp -o t && ./t

#include "usage_layout.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void should_stack_on_the_480_square_board() {
    // AMOLED-2.16: margin 20, content_y 100, panel_h 150, gap 16.
    UsageSlots s = usage_compute_slots(480, 480, 20, 100, 150, 16);
    CHECK(s.panel_w == 440);                 // 480 - 2*20
    CHECK(s.p1_x == 20 && s.p1_y == 100);
    CHECK(s.p2_x == 20 && s.p2_y == 266);    // 100 + 150 + 16
}

static void should_stack_on_the_368x448_portrait_board() {
    // AMOLED-1.8: margin 20, content_y 85, panel_h 130, gap 12.
    UsageSlots s = usage_compute_slots(368, 448, 20, 85, 130, 12);
    CHECK(s.panel_w == 328);
    CHECK(s.p2_y == 227);                    // 85 + 130 + 12
}

static void should_stack_on_the_240_square_board() {
    // LCD-1.54: margin 8, content_y 44, panel_h 74, gap 6.
    UsageSlots s = usage_compute_slots(240, 240, 8, 44, 74, 6);
    CHECK(s.panel_w == 224);
    CHECK(s.p1_x == 8 && s.p1_y == 44);
    CHECK(s.p2_y == 124);                    // 44 + 74 + 6
}

static void should_stack_full_width_on_the_800x480_landscape_board() {
    // LCD-4.3: margin 20, content_y 84, panel_h 130, gap 16. A wide screen
    // gets wider panels, NOT a different arrangement — the extra detail lives
    // on a second screen rather than beside the gauges.
    UsageSlots s = usage_compute_slots(800, 480, 20, 84, 130, 16);
    CHECK(s.panel_w == 760);                 // 800 - 2*20
    CHECK(s.p1_x == 20 && s.p1_y == 84);
    CHECK(s.p2_x == 20 && s.p2_y == 230);    // 84 + 130 + 16
    // Both panels share one column, so their x must match.
    CHECK(s.p1_x == s.p2_x);
}

static void should_never_overflow_the_screen_width() {
    const int16_t widths[] = {240, 368, 410, 480, 800};
    for (int i = 0; i < 5; ++i) {
        UsageSlots s = usage_compute_slots(widths[i], 480, 20, 84, 130, 16);
        CHECK(s.p1_x + s.panel_w == widths[i] - 20);
    }
}

int main() {
    should_stack_on_the_480_square_board();
    should_stack_on_the_368x448_portrait_board();
    should_stack_on_the_240_square_board();
    should_stack_full_width_on_the_800x480_landscape_board();
    should_never_overflow_the_screen_width();
    if (failures == 0) printf("all usage_layout tests passed\n");
    return failures != 0;
}
