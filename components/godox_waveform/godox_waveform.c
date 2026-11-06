/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "godox_waveform.h"
extern const uint32_t prepare_start[] asm("_binary_prepare_bin_start");
extern const uint32_t prepare_end[] asm("_binary_prepare_bin_end");
extern const uint32_t test_start[] asm("_binary_test_bin_start");
extern const uint32_t test_end[] asm("_binary_test_bin_end");
extern const uint32_t unprepare_start[] asm("_binary_unprepare_bin_start");
extern const uint32_t unprepare_end[] asm("_binary_unprepare_bin_end");
const uint32_t *godox_waveform_get(godox_command_t command, size_t *count)
{
    if (!count) return NULL;
    const uint32_t *start, *end;
    switch (command) {
    case GODOX_PREPARE: start = prepare_start; end = prepare_end; break;
    case GODOX_TEST: start = test_start; end = test_end; break;
    case GODOX_UNPREPARE: start = unprepare_start; end = unprepare_end; break;
    default: *count = 0; return NULL;
    }
    *count = end - start;
    return start;
}
