/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
/* Call first in app_main, including when the display UI is disabled. */
esp_err_t stage_board_power_init(void);
esp_err_t stage_board_start(void);
