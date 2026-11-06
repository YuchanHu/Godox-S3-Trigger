/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    bool initialized;
    bool fault;
    uint32_t last_window_us;
} rf_state_t;
/* TX operations belong to the RF worker. Atomic cancellation can be queried
 * by the capture task, which exclusively owns the receiver while scanning. */
esp_err_t rf_transmitter_init(void);
esp_err_t rf_transmitter_init_channel(unsigned wifi_channel);
esp_err_t rf_transmitter_send(const uint32_t *words, size_t count);
esp_err_t rf_transmitter_send_window(const uint32_t *words, size_t count, uint32_t window_us);
esp_err_t rf_transmitter_stop(void);
esp_err_t rf_transmitter_release(void);
void rf_transmitter_request_stop(void);
void rf_transmitter_clear_stop(void);
bool rf_transmitter_cancelled(void);
rf_state_t rf_transmitter_state(void);
