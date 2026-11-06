/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include "stage_frame.h"

/* Same command sequencer on the board and in the host controller regression tests. */
typedef struct {
    esp_err_t (*transfer)(void *ctx, bool data, const uint8_t *bytes, size_t count);
    esp_err_t (*reset)(void *ctx);
    esp_err_t (*wait_idle)(void *ctx);
    void *ctx;
} stage_epd_io_t;

/* Frames/scratch each contain 5000 bytes. Commit old_frame only after ESP_OK.
 * After any error the next call must use full=true to recover the RAM baseline. */
esp_err_t stage_epd_refresh(const stage_epd_io_t *io, bool full, const uint8_t *old_frame,
                            const uint8_t *frame, uint8_t *scratch, const stage_rect_t *dirty);
