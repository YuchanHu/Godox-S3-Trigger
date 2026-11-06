/* SPDX-License-Identifier: GPL-3.0-or-later
 * Burst capture adapted from ESPARGOS/esp-sdr 9b61e7465bee6da15209c5c09308043d08c46a1d.
 * Full source/attribution: third_party/esp-sdr-reference. Snapshot mode only;
 * shares the existing TX-reserved 64KiB aperture, never owns it concurrently.
 */
#include "rf_receiver.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "soc/soc.h"
extern void stop_tx_tone(unsigned), rom_pbus_workmode(void), rom_pbus_xpd_tx_off(void);
extern void set_chanfreq(unsigned, unsigned);
extern void rom_pbus_xpd_rx_on(unsigned), rom_set_rxclk_en(unsigned),
    force_rx_gain(unsigned, unsigned, unsigned);
static bool active;
static unsigned channel;
static int64_t prepared_at;
static rf_rx_metrics_t metrics;
rf_rx_metrics_t rf_receiver_metrics(void) { return metrics; }
#define RX_CTRL 0x60033d5cu
#define OWNER 0x600c101cu
#define WORDS 16128u
esp_err_t rf_receiver_release(void) {
    if (!active)
        return ESP_OK;
    REG_WRITE(RX_CTRL, 0);
    esp_err_t e = esp_wifi_set_promiscuous(false);
    esp_err_t s = esp_wifi_stop(), d = esp_wifi_deinit();
    active = false;
    channel = 0;
    prepared_at = 0;
    return e != ESP_OK ? e : s != ESP_OK ? s : d;
}
esp_err_t rf_receiver_init(void) {
    if (active)
        return ESP_ERR_INVALID_STATE;
    wifi_init_config_t c = WIFI_INIT_CONFIG_DEFAULT();
    c.nvs_enable = false;
    c.static_rx_buf_num = 4;
    c.dynamic_rx_buf_num = 8;
    c.dynamic_tx_buf_num = 8;
    esp_err_t e = esp_wifi_init(&c);
    if (e != ESP_OK)
        return e;
    active = true;
    if ((e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (e = esp_wifi_set_mode(WIFI_MODE_NULL)) != ESP_OK || (e = esp_wifi_start()) != ESP_OK ||
        (e = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK ||
        (e = esp_wifi_set_promiscuous(true)) != ESP_OK) {
        rf_receiver_release();
        return e;
    }
    return ESP_OK;
}
static esp_err_t capture(unsigned wifi, const uint32_t **samples, size_t *count, int8_t *packed,
                         unsigned rate) {
    if (!active || !samples || !count || wifi < 1 || wifi > 13)
        return ESP_ERR_INVALID_ARG;
    /* Keep consecutive snapshots on the prepared receiver. Re-running PHY tune
     * per1ms capture wastes airtime and restarts settling. Refresh periodically
     * during long verification dwells in case background PHY altered RX state. */
    int64_t now = esp_timer_get_time();
    if (channel != wifi || now - prepared_at >= 250000) {
        set_chanfreq(2412 + (wifi - 1) * 5, 0);
        stop_tx_tone(1);
        rom_pbus_workmode();
        rom_pbus_xpd_tx_off();
        rom_pbus_xpd_rx_on(1);
        rom_set_rxclk_en(1);
        force_rx_gain(0, 40, 0);
        esp_rom_delay_us(3000);
        channel = wifi;
        prepared_at = esp_timer_get_time();
    }
    volatile uint32_t *ram = (volatile uint32_t *)0x3fcd0000u;
    for (unsigned i = packed ? WORDS - 16 : 0; i < WORDS; i++)
        ram[i] = 0xa5a0055a;
    uint32_t owner = REG_READ(OWNER);
    REG_WRITE(RX_CTRL, 0);
    REG_WRITE(0x60033d90, 0x000c2040);
    REG_WRITE(OWNER, (owner & ~15u) | 4u);
    uint32_t ctrl = 0x80000000u | (rate == 40000000 ? (1u << 15) : (1u << 16)) | WORDS;
    REG_WRITE(RX_CTRL, ctrl);
    REG_WRITE(RX_CTRL, ctrl | (1u << 19));
    REG_WRITE(RX_CTRL, ctrl);
    int64_t begin = esp_timer_get_time(), until = begin + 20000;
    while (!(REG_READ(RX_CTRL) & (1u << 18)) && esp_timer_get_time() < until) {
    }
    bool done = (REG_READ(RX_CTRL) & (1u << 18)) != 0;
    metrics.elapsed_us = esp_timer_get_time() - begin;
    REG_WRITE(RX_CTRL, 0);
    REG_WRITE(OWNER, owner);
    __asm__ volatile("memw" ::: "memory");
    if (!done) {
        channel = 0;
        return ESP_ERR_TIMEOUT;
    }
    int32_t si = 0, sq = 0;
    uint64_t power = 0;
    uint32_t peak = 0, clips = 0;
    for (unsigned i = 0; i < WORDS; i++) {
        if (ram[i] == 0xa5a0055a)
            return ESP_ERR_INVALID_RESPONSE;
        uint32_t w = ram[i];
        if (packed) {
            packed[2 * i] = (int8_t)((w >> 2) & 255);
            packed[2 * i + 1] = (int8_t)((w >> 12) & 255);
        }
        int iv = w & 1023, qv = (w >> 10) & 1023;
        if (iv & 512)
            iv -= 1024;
        if (qv & 512)
            qv -= 1024;
        si += iv;
        sq += qv;
        uint32_t p = iv * iv + qv * qv;
        power += p;
        if (p > peak)
            peak = p;
        if (iv >= 500 || iv <= -500 || qv >= 500 || qv <= -500)
            clips++;
    }
    metrics.dc_i = si / WORDS;
    metrics.dc_q = sq / WORDS;
    int64_t variance =
        power / WORDS - (int64_t)metrics.dc_i * metrics.dc_i - (int64_t)metrics.dc_q * metrics.dc_q;
    metrics.variance = variance > 0 ? variance : 0;
    metrics.peak = peak;
    metrics.clipped = clips;
    *samples = (const uint32_t *)ram;
    *count = WORDS;
    return ESP_OK;
}

esp_err_t rf_receiver_capture(unsigned wifi, const uint32_t **samples, size_t *count) {
    return capture(wifi, samples, count, NULL, 16000000);
}
esp_err_t rf_receiver_capture_iq8(unsigned wifi, int8_t *samples, rf_rx_metrics_t *out) {
    if (!samples || !out)
        return ESP_ERR_INVALID_ARG;
    const uint32_t *raw;
    size_t count;
    esp_err_t e = capture(wifi, &raw, &count, samples, 16000000);
    if (e == ESP_OK)
        *out = metrics;
    return e;
}

esp_err_t rf_receiver_capture_iq8_rate(unsigned wifi, unsigned rate, int8_t *samples,
                                       rf_rx_metrics_t *out) {
    if (!samples || !out || (rate != 16000000 && rate != 40000000))
        return ESP_ERR_INVALID_ARG;
    const uint32_t *raw;
    size_t count;
    esp_err_t e = capture(wifi, &raw, &count, samples, rate);
    if (e == ESP_OK)
        *out = metrics;
    return e;
}
