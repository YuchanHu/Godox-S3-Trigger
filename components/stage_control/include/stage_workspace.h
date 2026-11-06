/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
/* RF worker only, after joining RX and stopping PLAY. Shares the existing
 * reserved RF aperture, so waveform creation needs no contiguous heap block.
 * Initialize the TX PHY before generating a waveform for immediate playback. */
uint32_t *stage_wave_workspace(void);
