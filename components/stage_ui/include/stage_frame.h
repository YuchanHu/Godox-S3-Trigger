/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    int x, y, w, h;
} stage_rect_t;
bool stage_frame_diff(const uint8_t *old_frame, const uint8_t *new_frame, stage_rect_t *rect);
size_t stage_frame_pack(const uint8_t *frame, const stage_rect_t *rect, uint8_t *out);
