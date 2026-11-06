/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned channel;        /* 2.4 GHz channel 1..13 */
    unsigned rf_backoff_qdb; /* Factory CW preparation argument, NOT output dBm */
} iq_tx_config_t;

typedef struct {
    const char *chip;
    uint32_t sample_rate_hz;
    size_t max_samples;
    size_t loop_alignment_samples;
    bool hardware_periodic;
    const uint32_t *supported_sample_rates_hz;
    size_t num_sample_rates;
} iq_tx_caps_t;

typedef struct {
    bool running;
    bool periodic;
    size_t samples;
    uint32_t playback_register;
} iq_tx_status_t;

/* One caller/task owns the driver. Do not mix with the Wi-Fi/BT stack or
 * another RF/PAU user. Initialize once per boot; no reconfiguration in flight.
 * Factory RF preparation briefly enables a CW before NCO is disabled. */
const iq_tx_caps_t *iq_tx_capabilities(void);
esp_err_t iq_tx_init(const iq_tx_config_t *config);
/* Stopped only. Selects native SRAM playback rate, without resampling IQ.
 * S3: 40/80 Msps. S31: 16/32/40/80 Msps. Applied before the next PLAY.
 * iq_tx_capabilities()->sample_rate_hz reports the currently selected rate. */
esp_err_t iq_tx_set_sample_rate(uint32_t sample_rate_hz);
esp_err_t iq_tx_set_channel(unsigned channel);
/* Stopped only. Copies packed words into reserved RF SRAM. Upper 12 bits = 0.
 * Each word: signed I10 in bits 9:0, signed Q10 in bits 19:10. */
esp_err_t iq_tx_load(const uint32_t *samples, size_t count);
esp_err_t iq_tx_start_loop(void);
/* S31 only: period >= waveform duration + 25 us. No per-period CPU ISR.
 * First burst follows one full period. Requires a previously loaded waveform. */
esp_err_t iq_tx_start_periodic(uint32_t period_us);
/* Stops future triggers, drains an in-flight hardware chain, disables RF.
 * Task context only; a bounded RTOS sleep is used on S31 before SRAM reuse. */
esp_err_t iq_tx_stop(void);
/* Godox-S3-Trigger addition: stop and release PHY/power; permits a later init. */
esp_err_t iq_tx_deinit(void);
/* S3 local extension: software bounded loop window, not a hardware one-shot. */
esp_err_t iq_tx_play_bounded(uint32_t window_us, uint32_t *elapsed_us);
iq_tx_status_t iq_tx_status(void);

static inline uint32_t iq_tx_pack(int16_t i, int16_t q)
{
    return ((uint32_t)i & 1023u) | (((uint32_t)q & 1023u) << 10);
}

#ifdef __cplusplus
}
#endif
