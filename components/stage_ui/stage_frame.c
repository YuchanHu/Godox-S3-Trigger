/* SPDX-License-Identifier: GPL-3.0-or-later; byte-aligned dirty rectangle. */
#include "stage_frame.h"
bool stage_frame_diff(const uint8_t *a, const uint8_t *b, stage_rect_t *r) {
    if (!a || !b || !r)
        return false;
    int minx = 25, maxx = -1, miny = 200, maxy = -1;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 25; x++)
            if (a[y * 25 + x] != b[y * 25 + x]) {
                if (x < minx)
                    minx = x;
                if (x > maxx)
                    maxx = x;
                if (y < miny)
                    miny = y;
                if (y > maxy)
                    maxy = y;
            }
    if (maxx < 0) {
        *r = (stage_rect_t){0};
        return false;
    }
    *r = (stage_rect_t){minx * 8, miny, (maxx - minx + 1) * 8, maxy - miny + 1};
    return true;
}
size_t stage_frame_pack(const uint8_t *frame, const stage_rect_t *r, uint8_t *out) {
    if (!frame || !r || !out || r->x < 0 || r->y < 0 || r->x > 200 || r->y > 200 || r->w <= 0 ||
        r->h <= 0 || r->w > 200 || r->h > 200 || r->x % 8 || r->w % 8 || r->x + r->w > 200 ||
        r->y + r->h > 200)
        return 0;
    size_t n = 0;
    for (int y = r->y; y < r->y + r->h; y++)
        for (int x = r->x / 8; x < (r->x + r->w) / 8; x++)
            out[n++] = frame[y * 25 + x];
    return n;
}
