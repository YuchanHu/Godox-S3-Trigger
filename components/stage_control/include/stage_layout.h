/* SPDX-License-Identifier: GPL-3.0-or-later; shared drawing and touch geometry. */
#pragma once
#include <stdbool.h>
typedef struct {
    int x, y, w, h;
} stage_area_t;
enum { STAGE_HEADER_H = 24, STAGE_FOOTER_Y = 180, STAGE_FOOTER_H = 20 };
static inline bool stage_area_contains(stage_area_t r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
static inline stage_area_t stage_home_card(int field) {
    return (stage_area_t){8 + field % 2 * 98, 36 + field / 2 * 50, 86, 40};
}
static inline stage_area_t stage_home_hit(int field) {
    stage_area_t r = stage_home_card(field);
    r.y -= 10; // Include the descriptor across the top border.
    r.h += 10;
    return r;
}
static inline stage_area_t stage_key_area(int key) {
    return (stage_area_t){8 + key % 3 * 62, 66 + key / 3 * 27, 60, 25};
}
static inline stage_area_t stage_option_area(int field, int index) {
    return (stage_area_t){8 + index % 2 * 96, 58 + index / 2 * (field == 1 ? 30 : 28), 88,
                          field == 1 ? 27 : 25};
}
static inline stage_area_t stage_edit_action(bool confirm) {
    return (stage_area_t){confirm ? 104 : 8, 178, 88, 22};
}
static inline stage_area_t stage_return_area(void) { return (stage_area_t){8, 158, 184, 19}; }
static inline stage_area_t stage_stop_area(void) { return (stage_area_t){0, 180, 200, 20}; }
