// Host unit test for usage-panel slot geometry. This is the one piece of
// shared UI code the LCD-4.3 port changes, so the existing three breakpoints
// are pinned here: a regression would silently reflow every other board.
//
//   g++ -std=c++17 -I ../../src test_main.cpp -o t && ./t

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
    CHECK(!s.two_col);
    CHECK(s.panel_w == 440);          // 480 - 2*20
    CHECK(s.p1_x == 20 && s.p1_y == 100);
    CHECK(s.p2_x == 20 && s.p2_y == 266);   // 100 + 150 + 16
}

static void should_stack_on_the_368x448_portrait_board() {
    // AMOLED-1.8: margin 20, content_y 85, panel_h 130, gap 12.
    UsageSlots s = usage_compute_slots(368, 448, 20, 85, 130, 12);
    CHECK(!s.two_col);
    CHECK(s.panel_w == 328);
    CHECK(s.p2_y == 227);             // 85 + 130 + 12
}

static void should_stack_on_the_240_square_board() {
    // LCD-1.54: margin 8, content_y 44, panel_h 74, gap 6.
    UsageSlots s = usage_compute_slots(240, 240, 8, 44, 74, 6);
    CHECK(!s.two_col);
    CHECK(s.panel_w == 224);
    CHECK(s.p1_x == 8 && s.p1_y == 44);
    CHECK(s.p2_y == 124);             // 44 + 74 + 6
}

static void should_use_two_columns_on_the_800x480_landscape_board() {
    // LCD-4.3: margin 20, content_y 100, panel_h 150, gap 16.
    UsageSlots s = usage_compute_slots(800, 480, 20, 100, 150, 16);
    CHECK(s.two_col);
    CHECK(s.panel_w == 372);          // (800 - 40 - 16) / 2
    CHECK(s.p1_x == 20 && s.p1_y == 100);
    CHECK(s.p2_x == 408 && s.p2_y == 100);   // 20 + 372 + 16, same row
}

static void should_not_use_two_columns_on_a_wide_but_narrow_panel() {
    // 640 wide is landscape but below the 700 threshold — stays stacked
    // rather than producing two cramped columns.
    UsageSlots s = usage_compute_slots(640, 480, 20, 100, 150, 16);
    CHECK(!s.two_col);
}

static void should_not_use_two_columns_on_a_tall_panel() {
    UsageSlots s = usage_compute_slots(480, 800, 20, 100, 150, 16);
    CHECK(!s.two_col);
}

int main() {
    should_stack_on_the_480_square_board();
    should_stack_on_the_368x448_portrait_board();
    should_stack_on_the_240_square_board();
    should_use_two_columns_on_the_800x480_landscape_board();
    should_not_use_two_columns_on_a_wide_but_narrow_panel();
    should_not_use_two_columns_on_a_tall_panel();
    if (failures == 0) printf("all usage_layout tests passed\n");
    return failures != 0;
}
