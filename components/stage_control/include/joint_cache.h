/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "stage_core.h"
#include "esp_err.h"
esp_err_t joint_cache_prepare(const stage_target_t *targets,unsigned count,uint32_t *work,
                              int (*progress)(unsigned,unsigned));
const uint32_t *joint_cache_wave(unsigned index);
void joint_cache_close(void);
