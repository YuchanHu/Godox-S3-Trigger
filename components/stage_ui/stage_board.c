/* SPDX-License-Identifier: GPL-3.0-or-later
 * Waveshare touch board wiring: official V2/13_FT6336_Test/user_config.h.
 * EPD register/LUT sequence: MIT Waveshare EPD_1in54_V2.c (see third_party).
 * UI/task implementation is local. Battery ADC follows the official V2 ADC1/CH3, x2 divider.
 */
#include "stage_board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stage_app.h"
#include "stage_epd.h"
#include "stage_frame.h"
#include "stage_view.h"
#include <stdatomic.h>
#include <string.h>
enum {
    EPD_POWER = 6,
    TP_RESET = 7,
    EPD_BUSY = 8,
    EPD_RESET = 9,
    EPD_DC = 10,
    EPD_CS = 11,
    EPD_SCK = 12,
    EPD_MOSI = 13,
    BOOT = 0,
    PWR = 18
};
static spi_device_handle_t spi;
static i2c_master_dev_handle_t touch;
static atomic_bool display_updating, first_frame_ready;
static atomic_bool blank_ready;
static uint8_t frame[5000], old_frame[5000], region[5000];
static const char *TAG = "panel";
static esp_err_t transfer(void *ctx, bool data, const uint8_t *bytes, size_t count) {
    (void)ctx;
    gpio_set_level(EPD_DC, data);
    spi_transaction_t transaction = {.length = count * 8, .tx_buffer = bytes};
    return spi_device_polling_transmit(spi, &transaction);
}
static esp_err_t wait_idle(void *ctx) {
    (void)ctx;
    // Allow BUSY to assert after activation; never commit a frame on the
    // pre-assertion low level immediately following command 0x20.
    vTaskDelay(pdMS_TO_TICKS(10));
    int64_t end = esp_timer_get_time() + 5000000;
    while (gpio_get_level(EPD_BUSY)) {
        if (esp_timer_get_time() > end)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return ESP_OK;
}
static esp_err_t reset_panel(void *ctx) {
    gpio_set_level(EPD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(EPD_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(EPD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    return wait_idle(ctx);
}
static esp_err_t refresh(bool full, const stage_rect_t *dirty) {
    const stage_epd_io_t io = {transfer, reset_panel, wait_idle, NULL};
    esp_err_t err = stage_epd_refresh(&io, full, old_frame, frame, region, dirty);
    if (!full && err == ESP_OK)
        ESP_LOGI(TAG, "partial x=%d y=%d w=%d h=%d bytes/bank=%u reference=synced", dirty->x,
                 dirty->y, dirty->w, dirty->h, (unsigned)(dirty->w / 8 * dirty->h));
    return err;
}
static void display_task(void *arg) {
    (void)arg;
    bool first = true;
    unsigned partials = 0;
    int64_t last = 0;
    stage_mode_t last_mode = STAGE_READY;
    bool last_scanning = false;
    unsigned last_targets = 0, last_list = 0;
    for (;;) {
        stage_ui_t s;
        stage_app_snapshot(&s, NULL);
        if (s.sleep_requested) {
            if (stage_app_quiescent() && !atomic_load(&blank_ready)) {
                memset(frame, 255, sizeof(frame));
                const stage_rect_t all = {0, 0, 200, 200};
                atomic_store(&display_updating, true);
                esp_err_t e = refresh(true, &all);
                if (e == ESP_OK) {
                    gpio_set_level(EPD_POWER, 1);
                    atomic_store(&blank_ready, true);
                } else
                    ESP_LOGE(TAG, "white-screen shutdown failed: %s", esp_err_to_name(e));
                atomic_store(&display_updating, false);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (s.scanning && last_scanning && s.target_count == last_targets &&
            s.list_page == last_list && esp_timer_get_time() - last < 1000000) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!s.preparing && s.mode == STAGE_RUNNING && last_mode == STAGE_RUNNING &&
            esp_timer_get_time() - last < 1000000) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        stage_view_render(&s, frame);
        stage_rect_t dirty;
        bool changed = stage_frame_diff(old_frame, frame, &dirty);
        if (!first && !changed)
            stage_app_displayed(&s);
        if (first || changed) {
            bool full =
                first || (partials >= 60 && s.mode == STAGE_READY && s.field < 0 && !s.preparing);
            if (full)
                dirty = (stage_rect_t){0, 0, 200, 200};
            atomic_store(&display_updating, true);
            esp_err_t err = refresh(full, &dirty);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "EPD refresh failed: %s; serial remains available",
                         esp_err_to_name(err));
                atomic_store(&first_frame_ready, false);
                first = true;
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else {
                memcpy(old_frame, frame, 5000);
                first = false;
                partials = full ? 0 : partials + 1;
                atomic_store(&first_frame_ready, true);
                stage_app_displayed(&s);
            }
            atomic_store(&display_updating, false);
            last = esp_timer_get_time();
            last_mode = s.mode;
            last_scanning = s.scanning;
            last_targets = s.target_count;
            last_list = s.list_page;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
static void battery_task(void *arg) {
    (void)arg;
    adc_oneshot_unit_handle_t unit = NULL;
    adc_cali_handle_t calibration = NULL;
    adc_oneshot_unit_init_cfg_t uc = {.unit_id = ADC_UNIT_1};
    adc_oneshot_chan_cfg_t cc = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    esp_err_t err = adc_oneshot_new_unit(&uc, &unit);
    if (err == ESP_OK)
        err = adc_oneshot_config_channel(unit, ADC_CHANNEL_3, &cc); // GPIO4
    adc_cali_curve_fitting_config_t cf = {.unit_id = ADC_UNIT_1,
                                          .chan = ADC_CHANNEL_3,
                                          .atten = ADC_ATTEN_DB_12,
                                          .bitwidth = ADC_BITWIDTH_12};
    if (err == ESP_OK)
        err = adc_cali_create_scheme_curve_fitting(&cf, &calibration);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Battery ADC unavailable: %s", esp_err_to_name(err));
        stage_app_battery_sample(0, false);
        if (unit)
            adc_oneshot_del_unit(unit);
        vTaskDelete(NULL);
        return;
    }
    int filtered = 0;
    unsigned loops = 0;
    for (;;) {
        int sum = 0;
        bool good = true;
        for (int n = 0; n < 16; n++) {
            int raw = 0, mv = 0;
            if (adc_oneshot_read(unit, ADC_CHANNEL_3, &raw) != ESP_OK ||
                adc_cali_raw_to_voltage(calibration, raw, &mv) != ESP_OK) {
                good = false;
                break;
            }
            sum += mv;
        }
        if (good) {
            int voltage = 2 * sum / 16;
            if (stage_battery_percent(voltage) < 0) {
                filtered = 0;
                stage_app_battery_sample(0, false);
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            filtered = filtered ? (3 * filtered + voltage) / 4 : voltage;
            stage_app_battery_sample(filtered, true);
            if (loops++ % 6 == 0)
                ESP_LOGI(TAG,
                         "battery=%d mV, estimate=%d%% (voltage-based, no charge-state detection)",
                         filtered, stage_battery_percent(filtered));
        } else
            stage_app_battery_sample(0, false);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
static void boot_task(void *arg) {
    (void)arg;
    int stable = gpio_get_level(BOOT), previous = stable;
    bool armed = stable != 0;
    unsigned same = 0;
    for (;;) {
        int raw = gpio_get_level(BOOT);
        same = raw == previous ? same + 1 : 0;
        previous = raw;
        if (same >= 3 && raw != stable) {
            stable = raw;
            if (stable)
                armed = true;
            else if (armed) {
                armed = false;
                stage_ui_t s;
                stage_app_snapshot(&s, NULL);
                if (s.mode == STAGE_RUNNING || atomic_load(&first_frame_ready))
                    stage_app_boot();
                else
                    ESP_LOGW(TAG, "BOOT start ignored until first display refresh succeeds");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
static void power_task(void *arg) {
    (void)arg;
    bool armed = gpio_get_level(PWR) != 0;
    unsigned down = 0, up = 0;
    for (;;) {
        bool released = gpio_get_level(PWR) != 0;
        if (released) {
            if (++up >= 5)
                armed = true;
            down = 0;
        } else {
            up = 0;
            if (armed && ++down >= 3) {
                armed = false;
                stage_app_power();
            }
        }
        if (atomic_load(&blank_ready) && released && up >= 5) {
            // Keep battery supply latched while the CPU sleeps; GPIO18 wakes on next press.
            ESP_ERROR_CHECK(rtc_gpio_init(GPIO_NUM_17));
            ESP_ERROR_CHECK(rtc_gpio_set_direction(GPIO_NUM_17, RTC_GPIO_MODE_OUTPUT_ONLY));
            ESP_ERROR_CHECK(rtc_gpio_set_level(GPIO_NUM_17, 1));
            ESP_ERROR_CHECK(rtc_gpio_hold_en(GPIO_NUM_17));
            ESP_ERROR_CHECK(rtc_gpio_init(GPIO_NUM_6));
            ESP_ERROR_CHECK(rtc_gpio_set_direction(GPIO_NUM_6, RTC_GPIO_MODE_OUTPUT_ONLY));
            ESP_ERROR_CHECK(rtc_gpio_set_level(GPIO_NUM_6, 1));
            ESP_ERROR_CHECK(rtc_gpio_hold_en(GPIO_NUM_6));
            ESP_ERROR_CHECK(rtc_gpio_init(GPIO_NUM_18));
            ESP_ERROR_CHECK(rtc_gpio_set_direction(GPIO_NUM_18, RTC_GPIO_MODE_INPUT_ONLY));
            ESP_ERROR_CHECK(rtc_gpio_pullup_en(GPIO_NUM_18));
            ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(GPIO_NUM_18));
            ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(GPIO_NUM_18, 0));
            esp_deep_sleep_start();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
static void touch_task(void *arg) {
    (void)arg;
    bool pressed = false;
    unsigned released = 0;
    for (;;) {
        uint8_t address = 2, data[5] = {0};
        esp_err_t err = i2c_master_transmit_receive(touch, &address, 1, data, 5, 50);
        if (err == ESP_OK) {
            if ((data[0] & 15) > 0) {
                released = 0;
                if (!pressed) {
                    pressed = true;
                    int x = ((data[1] & 15) << 8) | data[2], y = ((data[3] & 15) << 8) | data[4];
#if CONFIG_STAGE_TOUCH_SWAP_XY
                    int t = x;
                    x = y;
                    y = t;
#endif
#if CONFIG_STAGE_TOUCH_MIRROR_X
                    x = 199 - x;
#endif
#if CONFIG_STAGE_TOUCH_MIRROR_Y
                    y = 199 - y;
#endif
                    ESP_LOGI(TAG, "touch x=%d y=%d", x, y);
                    stage_ui_t s;
                    stage_app_snapshot(&s, NULL);
                    if (s.mode == STAGE_RUNNING || s.scanning ||
                        (!atomic_load(&display_updating) && atomic_load(&first_frame_ready)))
                        stage_app_touch(x, y);
                }
            } else if (++released >= 2)
                pressed = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t stage_board_start(void) {
    gpio_config_t out = {.pin_bit_mask = (1ULL << EPD_POWER) | (1ULL << EPD_RESET) |
                                         (1ULL << EPD_DC) | (1ULL << TP_RESET),
                         .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&out);
    if (err != ESP_OK)
        return err;
    gpio_set_level(EPD_POWER, 0); // Official board power enable is active-low
    gpio_set_level(EPD_RESET, 1);
    gpio_set_level(TP_RESET, 1);
    gpio_config_t input = {.pin_bit_mask = (1ULL << EPD_BUSY) | (1ULL << BOOT) | (1ULL << PWR),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE};
    err = gpio_config(&input);
    if (err != ESP_OK)
        return err;
    spi_bus_config_t bus = {.mosi_io_num = EPD_MOSI,
                            .miso_io_num = -1,
                            .sclk_io_num = EPD_SCK,
                            .quadwp_io_num = -1,
                            .quadhd_io_num = -1,
                            .max_transfer_sz = 5000};
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
        return err;
    spi_device_interface_config_t device = {
        .clock_speed_hz = 10000000, .mode = 0, .spics_io_num = EPD_CS, .queue_size = 1};
    err = spi_bus_add_device(SPI2_HOST, &device, &spi);
    if (err != ESP_OK)
        return err;
    i2c_master_bus_config_t ib = {.i2c_port = I2C_NUM_0,
                                  .sda_io_num = 47,
                                  .scl_io_num = 48,
                                  .clk_source = I2C_CLK_SRC_DEFAULT,
                                  .glitch_ignore_cnt = 7,
                                  .flags.enable_internal_pullup = true};
    i2c_master_bus_handle_t handle;
    esp_err_t touch_err = i2c_new_master_bus(&ib, &handle);
    if (touch_err == ESP_OK) {
        i2c_device_config_t td = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x38, .scl_speed_hz = 400000};
        touch_err = i2c_master_bus_add_device(handle, &td, &touch);
        gpio_set_level(TP_RESET, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(TP_RESET, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        if (touch_err == ESP_OK)
            touch_err = i2c_master_probe(handle, 0x38, 100);
    }
    if (xTaskCreatePinnedToCore(display_task, "epaper", 6144, NULL, 2, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(boot_task, "boot_key", 3072, NULL, 4, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(power_task, "power_key", 4096, NULL, 4, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(battery_task, "battery", 4096, NULL, 1, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (touch_err == ESP_OK) {
        if (xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 3, NULL, 0) != pdPASS)
            return ESP_ERR_NO_MEM;
    } else
        ESP_LOGE(TAG, "FT6336 unavailable: %s; BOOT and serial remain enabled",
                 esp_err_to_name(touch_err));
    ESP_LOGI(TAG, "Waveshare touch 200x200: SPI DC10 CS11 SCK12 MOSI13 RST9 BUSY8 PWR6; FT6336 "
                  "SDA47 SCL48 RST7; BOOT0");
    return ESP_OK;
}
