/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Start-to-start target. An overrun schedules the next attempt immediately,
 * without adding another full interval or accumulating catch-up attempts. */
int64_t stage_next_test_deadline(int64_t begin_us, int64_t now_us, uint32_t interval_ms,
                                 bool *limited);
