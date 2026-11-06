/* Test-only stand-in for the power initialization GPIO API. */
#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef int gpio_num_t;
enum { GPIO_NUM_17 = 17, GPIO_MODE_OUTPUT = 2, GPIO_PULLUP_ENABLE = 1,
       GPIO_PULLDOWN_DISABLE = 0, GPIO_INTR_DISABLE = 0 };
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
esp_err_t gpio_config(const gpio_config_t *config);
