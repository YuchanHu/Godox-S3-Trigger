/* SPDX-License-Identifier: GPL-3.0-or-later
 * Extracted from the verified S3 iq_sdr_prepare_tx / rf_timing_probe path.
 * Preserve vendor RF bring-up; the three explicit NCO clears are essential.
 */
#include "backend.h"
#include "esp_check.h"
#include "esp_phy_cert_test.h"
#include "esp_phy_init.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "heap_memory_layout.h"
#include "soc/periph_defs.h"
#include "soc/soc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE gate_mux = portMUX_INITIALIZER_UNLOCKED;

#define SRAM_BASE 0x3fcd0000u
#define SRAM_END 0x3fce0000u
#define OWNER 0x600c101cu
#define ADC 0x60033d5cu
#define PLAY 0x60033d64u
SOC_RESERVE_MEMORY_REGION(SRAM_BASE, SRAM_END, iq_tx_s3_sram);

extern void rom_pbus_debugmode(void), rom_pbus_workmode(void);
extern void rom_pbus_xpd_tx_on(unsigned, unsigned), rom_pbus_xpd_tx_off(void);
extern void rom_pbus_xpd_rx_off(void);
extern void rom_set_txclk_en(unsigned), rom_set_rxclk_en(unsigned), rom_dac_rate_set(unsigned);
extern void rom_loopback_mode_en(unsigned), rom_set_loopback_gain(unsigned, unsigned, unsigned);
extern void force_txon_mode(unsigned, unsigned, unsigned), stop_tx_tone(unsigned);
extern void set_chanfreq(unsigned, unsigned);

static iq_tx_config_t settings;
void iq_backend_set_channel(unsigned channel) { settings.channel=channel; }
static uint32_t owner_idle;
static bool phy_active, owner_saved;
static const uint32_t rates[] = {40000000, 80000000};
static iq_tx_caps_t caps = {"ESP32-S3", 40000000, 16128, 256, false, rates, 2};
static inline void fence(void)
{
    __asm__ volatile("memw" ::: "memory");
}
const iq_tx_caps_t *iq_backend_caps(void)
{
    return &caps;
}
void iq_backend_set_rate(uint32_t sample_rate_hz)
{
    caps.sample_rate_hz = sample_rate_hz;
}

esp_err_t iq_backend_stop(void)
{
    REG_WRITE(PLAY, 0);
    REG_WRITE(ADC, 0);
    stop_tx_tone(0);
    force_txon_mode(0, 0, 0);
    rom_loopback_mode_en(0);
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_off();
    rom_set_txclk_en(0);
    rom_set_rxclk_en(0);
    rom_pbus_workmode();
    REG_WRITE(OWNER, owner_idle);
    fence();
    return ESP_OK;
}

esp_err_t iq_backend_init(const iq_tx_config_t *config)
{
    settings = *config;
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    wifi.nvs_enable = false;
    wifi.static_rx_buf_num = 4;
    wifi.dynamic_rx_buf_num = 8;
    wifi.dynamic_tx_buf_num = 8;
    esp_err_t err = esp_wifi_init(&wifi);
    if (err != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK ||
        (err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return err;
    }
    err = esp_wifi_stop();
    esp_err_t deinit_err = esp_wifi_deinit();
    if (err != ESP_OK) return err;
    if (deinit_err != ESP_OK) return deinit_err;
    esp_wifi_power_domain_on();
    periph_module_enable(PERIPH_WIFI_MODULE);
    esp_phy_enable(PHY_MODEM_WIFI);
    phy_active = true;
    esp_phy_rftest_config(1);
    esp_phy_rftest_init();
    esp_phy_tx_contin_en(false);
    esp_phy_cbw40m_en(false);
    owner_idle = REG_READ(OWNER) & ~15u;
    owner_saved = true;
    return iq_backend_stop();
}

/* Local addition. Reverse the explicit PHY/peripheral/domain acquisitions above.
 * Repeated lifecycle and RF-off behaviour still require board measurement. */
esp_err_t iq_backend_deinit(void)
{
    if (owner_saved) iq_backend_stop();
    if (phy_active) {
        esp_phy_rftest_config(0);
        esp_phy_disable(PHY_MODEM_WIFI);
        periph_module_disable(PERIPH_WIFI_MODULE);
        esp_wifi_power_domain_off();
        phy_active = false;
    }
    owner_saved = false;
    return ESP_OK;
}

esp_err_t iq_backend_load(const uint32_t *samples, size_t count)
{
    REG_WRITE(OWNER, owner_idle);
    volatile uint32_t *dest = (volatile uint32_t *)SRAM_BASE;
    for (size_t n = 0; n < count; ++n) {
        dest[n] = samples[n];
    }
    fence();
    return ESP_OK;
}

static esp_err_t start_impl(size_t count, uint32_t period_us, uint32_t window_us, uint32_t *elapsed_us)
{
    if (period_us) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    unsigned dac_clock = caps.sample_rate_hz == 40000000 ? 1 : 0;
    set_chanfreq(2412 + 5 * (settings.channel - 1), 0);
    stop_tx_tone(0);
    rom_pbus_debugmode();
    rom_set_txclk_en(1);
    rom_dac_rate_set(dac_clock);
    rom_pbus_xpd_tx_on(0, 128);
    rom_set_rxclk_en(1);
    rom_set_loopback_gain(0, 0x104, 128);
    rom_loopback_mode_en(1);
    force_txon_mode(1, 0, 10);
    esp_rom_delay_us(20);
    rom_loopback_mode_en(0);
    esp_phy_wifi_tx_tone(1, settings.channel, settings.rf_backoff_qdb);
    rom_set_txclk_en(1);
    rom_set_rxclk_en(0);
    rom_dac_rate_set(dac_clock);
    REG_CLR_BIT(0x60006040, BIT(18));
    REG_CLR_BIT(0x60006044, BIT(18));
    REG_CLR_BIT(0x6000604c, BIT(18));
    REG_WRITE(ADC, 0);
    REG_WRITE(PLAY, 0);
    REG_WRITE(OWNER, owner_idle | 4u);
    uint32_t control = (uint32_t)count | (((uint32_t)count & 255u) << 20) | BIT(19);
    if (!dac_clock) {
        control |= BIT(15);
    }
    REG_WRITE(PLAY, control);
    fence();
    /* Local bounded-window extension: PHY setup stays outside the critical
     * section. Enter BEFORE asserting PLAY, eliminating the return-to-caller
     * scheduling gap. S3 stop uses registers/ROM, not RTOS services. */
    if (window_us) portENTER_CRITICAL(&gate_mux);
    int64_t begin = window_us ? esp_timer_get_time() : 0;
    REG_WRITE(PLAY, control | BIT(31));
    fence();
    if (window_us) {
        while ((esp_timer_get_time() - begin) < window_us) esp_rom_delay_us(1);
        iq_backend_stop();
        *elapsed_us = (uint32_t)(esp_timer_get_time() - begin);
        portEXIT_CRITICAL(&gate_mux);
    }
    return ESP_OK;
}
esp_err_t iq_backend_start(size_t count, uint32_t period_us)
{
    return start_impl(count, period_us, 0, NULL);
}
esp_err_t iq_backend_play_bounded(size_t count, uint32_t window_us, uint32_t *elapsed_us)
{
    return start_impl(count, 0, window_us, elapsed_us);
}
uint32_t iq_backend_playback_register(void)
{
    return REG_READ(PLAY);
}
