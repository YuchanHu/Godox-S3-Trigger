/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
esp_err_t rf_receiver_init(void);
esp_err_t rf_receiver_capture(unsigned wifi_channel, const uint32_t **samples, size_t *count);
esp_err_t rf_receiver_release(void);
typedef struct {
    uint32_t elapsed_us, variance, peak, clipped;
    int32_t dc_i, dc_q;
} rf_rx_metrics_t;
/* Single RF worker only, after a successful capture. ADC codes, not dBm. */
rf_rx_metrics_t rf_receiver_metrics(void);

esp_err_t rf_receiver_capture_iq8(unsigned wifi_channel, int8_t *samples, rf_rx_metrics_t *metrics);
esp_err_t rf_receiver_capture_iq8_rate(unsigned wifi, unsigned rate, int8_t *samples,
                                       rf_rx_metrics_t *metrics);
