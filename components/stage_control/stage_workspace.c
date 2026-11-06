/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage_workspace.h"
#include "godox_dsp.h"
uint32_t *stage_wave_workspace(void) {
#ifdef ESP_PLATFORM
    return (uint32_t *)0x3fcd0000u;
#else
    static uint32_t words[GODOX_DSP_WORDS];
    return words;
#endif
}
