/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rf_transmitter.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "godox_waveform.h"
#include "iq_tx.h"
#include <stdatomic.h>
static atomic_bool cancel;
static unsigned selected_channel;
static struct {
    atomic_bool initialized;
    atomic_bool fault;
    atomic_uint_least32_t last_window_us;
} state;
void rf_transmitter_request_stop(void) { atomic_store(&cancel, true); }
void rf_transmitter_clear_stop(void) { atomic_store(&cancel, false); }
bool rf_transmitter_cancelled(void) { return atomic_load(&cancel); }
rf_state_t rf_transmitter_state(void) {
    return (rf_state_t){state.initialized, state.fault, state.last_window_us};
}
esp_err_t rf_transmitter_init(void) { return rf_transmitter_init_channel(3); }
esp_err_t rf_transmitter_init_channel(unsigned wifi_channel) {
    if (wifi_channel < 1 || wifi_channel > 13)
        return ESP_ERR_INVALID_ARG;
    if (state.fault)
        return ESP_ERR_INVALID_STATE;
    if (state.initialized) {
        if(selected_channel==wifi_channel) return ESP_OK;
        esp_err_t e=rf_transmitter_stop();
        if(e==ESP_OK) e=iq_tx_set_channel(wifi_channel);
        if(e==ESP_OK) selected_channel=wifi_channel;
        return e;
    }
    if (rf_transmitter_cancelled())
        return ESP_ERR_INVALID_STATE;
    ESP_LOGW("rf",
             "RF init; private PHY path, output power uncalibrated. CW preparation on each start");
    const iq_tx_config_t cfg = {.channel = wifi_channel, .rf_backoff_qdb = 60};
    esp_err_t err = iq_tx_init(&cfg);
    if (err != ESP_OK) {
        state.fault = true;
        return err;
    }
    state.initialized = true;
    selected_channel = wifi_channel;
    return iq_tx_set_sample_rate(GODOX_SAMPLE_RATE);
}
esp_err_t rf_transmitter_stop(void) {
    if (!state.initialized)
        return state.fault ? ESP_ERR_INVALID_STATE : ESP_OK;
    esp_err_t err = iq_tx_stop();
    if (err != ESP_OK)
        state.fault = true;
    return err;
}
esp_err_t rf_transmitter_release(void) {
    esp_err_t err = rf_transmitter_stop();
    if (state.initialized) {
        esp_err_t release_err = iq_tx_deinit();
        if (err == ESP_OK)
            err = release_err;
        state.initialized = false;
        selected_channel = 0;
    }
    if (err != ESP_OK)
        state.fault = true;
    return err;
}
esp_err_t rf_transmitter_send(const uint32_t *words, size_t count) {
    return rf_transmitter_send_window(words, count, GODOX_WINDOW_US);
}
esp_err_t rf_transmitter_send_window(const uint32_t *words, size_t count, uint32_t window_us) {
    if (!state.initialized || state.fault || rf_transmitter_cancelled())
        return ESP_ERR_INVALID_STATE;
    if (count != GODOX_SAMPLE_COUNT)
        return ESP_ERR_INVALID_SIZE;
    if (window_us < 1 || window_us > 400)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = iq_tx_load(words, count);
    if (err != ESP_OK) {
        rf_transmitter_stop();
        return err;
    }
    if (rf_transmitter_cancelled())
        return ESP_ERR_INVALID_STATE;
    /* Local backend gates PLAY under a short critical section, after vendor
     * preparation, so no task-switch gap exists between PLAY and its deadline. */
    int64_t begin = esp_timer_get_time();
    uint32_t elapsed = 0;
    err = iq_tx_play_bounded(window_us, &elapsed);
    state.last_window_us = elapsed;
    if (err != ESP_OK) {
        rf_transmitter_stop();
        return err;
    }
    ESP_LOGD("rf", "start+PLAY+stop wall duration=%lld us", esp_timer_get_time() - begin);
    ESP_LOGD("rf", "PLAY+cleanup=%lu us; gate=%lu us; loop=403.2 us (not an on-air loop count)",
             (unsigned long)state.last_window_us, (unsigned long)window_us);
    return rf_transmitter_cancelled() ? ESP_ERR_INVALID_STATE : ESP_OK;
}
