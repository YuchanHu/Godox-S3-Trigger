/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "iq_tx.h"
const iq_tx_caps_t *iq_backend_caps(void);
esp_err_t iq_backend_init(const iq_tx_config_t *config);
void iq_backend_set_rate(uint32_t sample_rate_hz);
void iq_backend_set_channel(unsigned channel);
esp_err_t iq_backend_load(const uint32_t *samples, size_t count);
esp_err_t iq_backend_start(size_t count, uint32_t period_us);
esp_err_t iq_backend_stop(void);
esp_err_t iq_backend_deinit(void);
esp_err_t iq_backend_play_bounded(size_t count, uint32_t window_us, uint32_t *elapsed_us);
uint32_t iq_backend_playback_register(void);
