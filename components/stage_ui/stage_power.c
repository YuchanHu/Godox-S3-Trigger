/* SPDX-License-Identifier: GPL-3.0-or-later
 * Waveshare ESP32-S3-Touch-ePaper-1.54: BAT_Control GPIO17, active high.
 * See official schematic and pinned port_power.cpp BoardPower_VBAT_ON().
 */
#include "driver/gpio.h"
#include "stage_board.h"
#ifdef ESP_PLATFORM
#include "driver/rtc_io.h"
#endif

esp_err_t stage_board_power_init(void) {
    // Preload the high latch before enabling output, avoiding a low pulse.
    esp_err_t err = gpio_set_level(GPIO_NUM_17, 1);
    if (err != ESP_OK)
        return err;
#ifdef ESP_PLATFORM
    rtc_gpio_deinit(GPIO_NUM_17);
    rtc_gpio_deinit(GPIO_NUM_18);
    rtc_gpio_hold_dis(GPIO_NUM_6);
    rtc_gpio_deinit(GPIO_NUM_6);
#endif
    const gpio_config_t config = {.pin_bit_mask = 1ULL << GPIO_NUM_17,
                                  .mode = GPIO_MODE_OUTPUT,
                                  .pull_up_en = GPIO_PULLUP_ENABLE,
                                  .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                  .intr_type = GPIO_INTR_DISABLE};
    err = gpio_config(&config);
#ifdef ESP_PLATFORM
    if (err == ESP_OK)
        rtc_gpio_hold_dis(GPIO_NUM_17);
#endif
    return err;
}
