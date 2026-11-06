/* SPDX-License-Identifier: GPL-3.0-or-later
 * A fixed 64-sample circle demonstrates native sample rate selection.
 * Its RF offset is Fs/64: changing Fs changes the actual transmitted tone.
 */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_console.h"
#include "iq_tx.h"

static uint32_t tone[1024];
static int stop_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return iq_tx_stop() != ESP_OK;
}
static int tone_cmd(int argc, char **argv)
{
    if (argc != 2) {
        return 1;
    }
    char *end;
    unsigned long mhz = strtoul(argv[1], &end, 10);
    if (*end || mhz > 80) {
        return 1;
    }
    esp_err_t err = iq_tx_stop();
    if (err == ESP_OK) {
        err = iq_tx_set_sample_rate((uint32_t)mhz * 1000000);
    }
    if (err == ESP_OK) {
        err = iq_tx_load(tone, 1024);
    }
    if (err == ESP_OK) {
        err = iq_tx_start_loop();
    }
    printf("tone: %s rate=%" PRIu32 " Hz expected_offset=%" PRIu32 " Hz\n",
           esp_err_to_name(err),
           iq_tx_capabilities()->sample_rate_hz,
           iq_tx_capabilities()->sample_rate_hz / 64);
    return err != ESP_OK;
}
void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const iq_tx_config_t config = {.channel = CONFIG_EXAMPLE_TX_CHANNEL,
                                   .rf_backoff_qdb = CONFIG_EXAMPLE_RF_BACKOFF_QDB};
    ESP_ERROR_CHECK(iq_tx_init(&config));
    for (unsigned n = 0; n < 1024; ++n) {
        float phase = 2 * 3.14159265358979323846f * (n % 64) / 64;
        tone[n] =
            iq_tx_pack((int16_t)lroundf(64 * cosf(phase)), (int16_t)lroundf(64 * sinf(phase)));
    }
    const iq_tx_caps_t *caps = iq_tx_capabilities();
    printf("%s supported native rates (Msps):", caps->chip);
    for (size_t n = 0; n < caps->num_sample_rates; ++n) {
        printf(" %" PRIu32, caps->supported_sample_rates_hz[n] / 1000000);
    }
    printf("\nCommands: tone <Msps>, stop. Boot state: stopped.\n");
    esp_console_repl_t *repl;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "rates>";
    rc.task_stack_size = 8192;
    rc.task_core_id = 0;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &rc, &repl));
    const esp_console_cmd_t commands[] = {
        {.command = "tone",
         .help = "Native Fs in Msps: S3 40/80; S31 16/32/40/80",
         .func = tone_cmd},
        {.command = "stop", .help = "Stop playback", .func = stop_cmd},
    };
    for (size_t n = 0; n < 2; ++n) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[n]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
