/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "backend.h"

static bool initialized, running, periodic;
static size_t loaded;

const iq_tx_caps_t *iq_tx_capabilities(void)
{
    return iq_backend_caps();
}

esp_err_t iq_tx_init(const iq_tx_config_t *config)
{
    if (!config || config->channel < 1 || config->channel > 13 || config->rf_backoff_qdb > 120) {
        return ESP_ERR_INVALID_ARG;
    }
    if (initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = iq_backend_init(config);
    if (err == ESP_OK) {
        initialized = true;
    }
    return err;
}

esp_err_t iq_tx_load(const uint32_t *samples, size_t count)
{
    if (!initialized || running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!samples || !count || count > iq_backend_caps()->max_samples) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Validate before touching SRAM, so a rejected load preserves old data. */
    for (size_t n = 0; n < count; ++n) {
        if (samples[n] & 0xfff00000u) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    esp_err_t err = iq_backend_load(samples, count);
    if (err == ESP_OK) {
        loaded = count;
    }
    return err;
}

esp_err_t iq_tx_set_sample_rate(uint32_t sample_rate_hz)
{
    if (!initialized || running) {
        return ESP_ERR_INVALID_STATE;
    }
    const iq_tx_caps_t *caps = iq_backend_caps();
    for (size_t n = 0; n < caps->num_sample_rates; ++n) {
        if (caps->supported_sample_rates_hz[n] == sample_rate_hz) {
            iq_backend_set_rate(sample_rate_hz);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t iq_tx_set_channel(unsigned channel) {
    if (!initialized || running) return ESP_ERR_INVALID_STATE;
    if (channel<1 || channel>13) return ESP_ERR_INVALID_ARG;
    iq_backend_set_channel(channel);
    return ESP_OK;
}

static esp_err_t start(uint32_t period_us)
{
    const iq_tx_caps_t *caps = iq_backend_caps();
    if (!initialized || running || !loaded) {
        return ESP_ERR_INVALID_STATE;
    }
    if (period_us) {
        if (!caps->hardware_periodic) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        uint64_t duration =
            ((uint64_t)loaded * 1000000 + caps->sample_rate_hz - 1) / caps->sample_rate_hz;
        if (period_us < duration + 25 || period_us > 1000000) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (loaded % caps->loop_alignment_samples) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = iq_backend_start(loaded, period_us);
    if (err == ESP_OK) {
        running = true;
        periodic = period_us != 0;
    }
    return err;
}

esp_err_t iq_tx_start_loop(void)
{
    return start(0);
}
esp_err_t iq_tx_start_periodic(uint32_t period_us)
{
    return period_us ? start(period_us) : ESP_ERR_INVALID_ARG;
}
esp_err_t iq_tx_stop(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = iq_backend_stop();
    if (err == ESP_OK) {
        running = false;
        periodic = false;
    }
    return err;
}
iq_tx_status_t iq_tx_status(void)
{
    return (iq_tx_status_t){.running = running,
                            .periodic = periodic,
                            .samples = loaded,
                            .playback_register = initialized ? iq_backend_playback_register() : 0};
}

/* Local lifecycle extension; see third_party/README.md. */
esp_err_t iq_tx_deinit(void)
{
    if (!initialized) return ESP_OK;
    esp_err_t err = iq_backend_deinit();
    if (err == ESP_OK) {
        initialized = running = periodic = false;
        loaded = 0;
    }
    return err;
}

esp_err_t iq_tx_play_bounded(uint32_t window_us, uint32_t *elapsed_us)
{
    const iq_tx_caps_t *caps = iq_backend_caps();
    if (!initialized || running || !loaded) return ESP_ERR_INVALID_STATE;
    if (!elapsed_us || !window_us || window_us > 1000 || loaded % caps->loop_alignment_samples)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = iq_backend_play_bounded(loaded, window_us, elapsed_us);
    running = periodic = false;
    return err;
}
