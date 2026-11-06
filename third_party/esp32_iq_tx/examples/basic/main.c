/* SPDX-License-Identifier: GPL-3.0-or-later
 * Minimal API use with caller-owned data: a 1024-point QPSK staircase.
 * No OFDM framing. Each constellation point is held for 16 native samples.
 */
#include <stdio.h>
#include "esp_console.h"
#include "iq_tx.h"

static uint32_t waveform[1024];
static int send_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ESP_ERROR_CHECK(iq_tx_stop());
    ESP_ERROR_CHECK(iq_tx_load(waveform, 1024));
    ESP_ERROR_CHECK(iq_tx_start_loop());
    puts("Custom packed IQ10 waveform running");
    return 0;
}
static int stop_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return iq_tx_stop() != ESP_OK;
}
void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const iq_tx_config_t config = {.channel = CONFIG_EXAMPLE_TX_CHANNEL,
                                   .rf_backoff_qdb = CONFIG_EXAMPLE_RF_BACKOFF_QDB};
    ESP_ERROR_CHECK(iq_tx_init(&config));
    for (unsigned n = 0; n < 1024; ++n) {
        unsigned symbol = (n / 16) % 4;
        waveform[n] = iq_tx_pack(symbol & 1 ? 64 : -64, symbol & 2 ? 64 : -64);
    }
    esp_console_repl_t *repl;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "basic>";
    rc.task_stack_size = 8192;
    rc.task_core_id = 0;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &rc, &repl));
    const esp_console_cmd_t commands[] = {
        {.command = "send", .help = "Load and loop caller-provided IQ10 data", .func = send_cmd},
        {.command = "stop", .help = "Stop playback", .func = stop_cmd},
    };
    for (size_t n = 0; n < 2; ++n) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[n]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    puts("Caller-owned waveform example. Boot state: stopped.");
}
