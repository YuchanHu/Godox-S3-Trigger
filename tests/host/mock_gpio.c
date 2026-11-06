/* Test-only GPIO latch/direction model and error injection. */
#include "driver/gpio.h"
static int calls, fail_at, level, enabled, low_pulse, unrelated;
void mock_gpio_reset(int failure) {
    calls = level = enabled = low_pulse = unrelated = 0;
    fail_at = failure;
}
int mock_gpio_calls(void) { return calls; }
int mock_gpio_level(void) { return level; }
int mock_gpio_enabled(void) { return enabled; }
int mock_gpio_low_pulse(void) { return low_pulse; }
int mock_gpio_unrelated(void) { return unrelated; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t value) {
    if (++calls == fail_at) return ESP_FAIL;
    if (pin != 17) unrelated++;
    level = value;
    if (enabled && !level) low_pulse++;
    return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t *c) {
    if (++calls == fail_at) return ESP_FAIL;
    if (c->pin_bit_mask != (1ULL << 17)) unrelated++;
    if (c->mode != GPIO_MODE_OUTPUT || c->pull_up_en != GPIO_PULLUP_ENABLE ||
        c->pull_down_en || c->intr_type) return ESP_ERR_INVALID_ARG;
    enabled = 1;
    if (!level) low_pulse++;
    return ESP_OK;
}
