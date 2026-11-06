/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#define GODOX_TEST_GAP_DEFAULT_MS 2u
#define GODOX_TEST_GAP_MIN_MS 100u
#define GODOX_TEST_GAP_MAX_MS 3000u
esp_err_t godox_trigger_fire_test(void);
/* Custom gap is end-of-first-send to start of second-send preparation.
 * No RF playback during the gap. Does not promise optical flash timing. */
esp_err_t godox_trigger_fire_test_with_gap(uint32_t test_gap_ms);
void godox_trigger_dryrun(void);
esp_err_t godox_trigger_dryrun_with_gap(uint32_t test_gap_ms);
