/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "godox_trigger.h"
#include <stdio.h>
#include "godox_waveform.h"
#include "rf_transmitter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* Experimental end-to-start gaps: reference documents repeats but does not specify
 * TEST gaps. These values must be checked against a real transmitter capture. */
typedef struct { godox_command_t command; const char *name; unsigned gap_ms; } step_t;
static const step_t sequence[] = {
    {GODOX_PREPARE, "prepare", 20}, {GODOX_TEST, "test 1/2", 2},
    {GODOX_TEST, "test 2/2", 20}, {GODOX_UNPREPARE, "unprepare 1/2", 2},
    {GODOX_UNPREPARE, "unprepare 2/2", 0}
};
static bool valid_gap(uint32_t gap_ms)
{
    return gap_ms == GODOX_TEST_GAP_DEFAULT_MS ||
        (gap_ms >= GODOX_TEST_GAP_MIN_MS && gap_ms <= GODOX_TEST_GAP_MAX_MS);
}
esp_err_t godox_trigger_dryrun_with_gap(uint32_t test_gap_ms)
{
    if (!valid_gap(test_gap_ms)) return ESP_ERR_INVALID_ARG;
    puts("DRYRUN: CH08 2424.5 MHz = Wi-Fi CH3 2422 MHz + assumed positive IQ 2.5 MHz; ID OFF");
    puts("Short commands broadcast to matching ID; group A is a receiver setting, not in this frame.");
    for (size_t n = 0; n < sizeof(sequence)/sizeof(sequence[0]); ++n) {
        uint8_t bytes[10], bits[81];
        godox_short_frame(0, sequence[n].command, bytes, bits);
        printf("%s: ", sequence[n].name);
        for (unsigned b = 0; b < sizeof(bytes); ++b) printf("%02X ", bytes[b]);
        printf("+%u; 81 bits/324 us, shaped 339 us; IQ=16128 @40MS/s, loop=403.2 us, gate=380 us, gap=%u ms\n",
               bits[80], sequence[n].command == GODOX_TEST && n == 1 ? (unsigned)test_gap_ms : sequence[n].gap_ms);
    }
    printf("5 bounded software windows (nominal 1900 us PLAY total) + %lu ms gaps + PHY/load overhead.\n",
           (unsigned long)(42 + test_gap_ms));
    puts("CW during start, stop latency, loop phase and frequency sign require RF measurement; no single-frame guarantee.");
    return ESP_OK;
}
void godox_trigger_dryrun(void)
{
    (void)godox_trigger_dryrun_with_gap(GODOX_TEST_GAP_DEFAULT_MS);
}
esp_err_t godox_trigger_fire_test_with_gap(uint32_t test_gap_ms)
{
    if (!valid_gap(test_gap_ms)) return ESP_ERR_INVALID_ARG;
    ESP_LOGI("trigger", "TEST inter-command gap=%lu ms (RF stopped while waiting)", (unsigned long)test_gap_ms);
    esp_err_t err = rf_transmitter_init();
    if (err != ESP_OK) goto done;
    for (size_t n = 0; n < sizeof(sequence)/sizeof(sequence[0]); ++n) {
        if (rf_transmitter_cancelled()) { err = ESP_ERR_INVALID_STATE; break; }
        size_t count;
        const uint32_t *words = godox_waveform_get(sequence[n].command, &count);
        ESP_LOGI("trigger", "load/transmit %s at t=%lld us", sequence[n].name, esp_timer_get_time());
        err = rf_transmitter_send(words, count);
        if (err != ESP_OK) break;
        unsigned gap_ms = n == 1 ? test_gap_ms : sequence[n].gap_ms;
        if (n == 1 && test_gap_ms != GODOX_TEST_GAP_DEFAULT_MS)
            ESP_LOGI("trigger", "test 1/2 sent; RF stopped, waiting %u ms before test 2/2; stop cancels", gap_ms);
        int64_t until = esp_timer_get_time() + (int64_t)gap_ms * 1000;
        while (esp_timer_get_time() < until && !rf_transmitter_cancelled()) vTaskDelay(1);
    }
done:;
    esp_err_t cleanup = rf_transmitter_release();
    if (err == ESP_OK) err = cleanup;
    ESP_LOGI("trigger", "sequence ended, RF stopped/released: %s; flash response unobserved", esp_err_to_name(err));
    return err;
}
esp_err_t godox_trigger_fire_test(void)
{
    return godox_trigger_fire_test_with_gap(GODOX_TEST_GAP_DEFAULT_MS);
}
