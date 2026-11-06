/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage_timing.h"
int64_t stage_next_test_deadline(int64_t begin_us, int64_t now_us, uint32_t interval_ms,
                                 bool *limited) {
    int64_t desired = begin_us + (int64_t)interval_ms * 1000;
    if (now_us > desired) {
        if (limited)
            *limited = true;
        return now_us;
    }
    return desired;
}
