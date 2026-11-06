/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GODOX_DSP_WORDS 16128
/* No heap, GPIO or RF. Single caller; scratch storage is shared. */
bool godox_dsp_generate(const uint8_t *bits, size_t bit_count, int32_t offset_hz,
                        uint32_t words[GODOX_DSP_WORDS], uint32_t *window_us);
