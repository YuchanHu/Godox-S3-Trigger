/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include "stage_core.h"
esp_err_t stage_app_init(bool flash_ok);
esp_err_t stage_app_start(void);
esp_err_t stage_app_legacy_fire(uint32_t gap_ms);
void stage_app_stop(void);
void stage_app_boot(void);
void stage_app_touch(int x, int y);
void stage_app_snapshot(stage_ui_t *s, bool *busy);
esp_err_t stage_app_set(const stage_config_t *config);
esp_err_t stage_app_prepare(void);
void stage_app_displayed(const stage_ui_t *snapshot);
void stage_app_battery_sample(int millivolts, bool valid);
esp_err_t stage_app_scan(void);
esp_err_t stage_app_rx_dump(void);
esp_err_t stage_app_scan_channel(unsigned channel);
esp_err_t stage_app_clear_targets(void);
void stage_app_shutdown(void);
void stage_app_power(void);
bool stage_app_quiescent(void);
