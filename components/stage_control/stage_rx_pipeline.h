/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "rf_receiver.h"
typedef struct stage_rx_pipeline stage_rx_pipeline_t;
typedef struct {
    int8_t *iq;
    unsigned band, index;
    uint32_t captures, errors;
    uint64_t listening_us, waiting_us;
    esp_err_t error;
    rf_rx_metrics_t metrics;
    unsigned wifi, focus_ch, rate_hz;
} stage_rx_sample_t;
esp_err_t stage_rx_open(unsigned fixed_ch, int64_t deadline, stage_rx_pipeline_t **out);
/* Waits at most20ms. NOT_FOUND means producer finished and queue empty. */
esp_err_t stage_rx_next(stage_rx_pipeline_t *p, const stage_rx_sample_t **sample);
void stage_rx_recycle(stage_rx_pipeline_t *p, const stage_rx_sample_t *sample);
esp_err_t stage_rx_close(stage_rx_pipeline_t *p);
void stage_rx_focus(stage_rx_pipeline_t *p, unsigned ch, unsigned observed_wifi);
