/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "stage_core.h"
#define STAGE_FRAME_BYTES 5000
void stage_view_render(const stage_ui_t *s, uint8_t frame[STAGE_FRAME_BYTES]);
