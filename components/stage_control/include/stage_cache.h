/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include "stage_cache_plan.h"
/* Completed work units: erase + each compute/write + header + each readback.
 * Wave/group identify the operation actually about to run (or just completed).
 * Return nonzero to continue, zero to cancel. Use int for an unambiguous callback ABI. */
typedef int (*stage_cache_progress_t)(stage_prep_phase_t phase, unsigned done, unsigned total,
                                      stage_prep_wave_t wave, unsigned group);
esp_err_t stage_cache_open(const stage_config_t *c);
esp_err_t stage_cache_open_report(const stage_config_t *c, stage_cache_progress_t progress);
esp_err_t stage_cache_build(const stage_config_t *c, stage_cache_progress_t progress);
const stage_cache_header_t *stage_cache_header(void);
const uint32_t *stage_cache_words(unsigned index);
void stage_cache_close(void);
