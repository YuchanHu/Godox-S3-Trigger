/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "godox_protocol.h"
#include "godox_trigger.h"
#include "nvs_flash.h"
#include "rf_transmitter.h"
#include "stage_app.h"
#include "stage_board.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static bool number(const char *text, unsigned long *out) {
    if (!text || text[0] < '0' || text[0] > '9')
        return false;
    errno = 0;
    char *end;
    *out = strtoul(text, &end, 10);
    return !errno && !*end;
}
static int fire_cmd(int argc, char **argv) {
    unsigned long gap = GODOX_TEST_GAP_DEFAULT_MS;
    if (argc > 2 || (argc == 2 && (!number(argv[1], &gap) || gap < GODOX_TEST_GAP_MIN_MS ||
                                   gap > GODOX_TEST_GAP_MAX_MS))) {
        puts("fire [100..3000 ms]; omit for legacy 2ms");
        return 1;
    }
    esp_err_t e = stage_app_legacy_fire(gap);
    printf("Legacy TEST sequence: %s; fixed CH08/ID OFF, two TEST commands\n", esp_err_to_name(e));
    return e != ESP_OK;
}
static int stop_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    stage_app_stop();
    puts("Stop requested; worker will release RF");
    return 0;
}
static int stage_cmd(int argc, char **argv) {
    (void)argv;
    if (argc != 1)
        return 1;
    esp_err_t e = stage_app_start();
    printf("Finite stage sequence: %s\n", esp_err_to_name(e));
    return e != ESP_OK;
}
static int dryrun_cmd(int argc, char **argv) {
    unsigned long gap = GODOX_TEST_GAP_DEFAULT_MS;
    if (argc > 2 || (argc == 2 && (!number(argv[1], &gap) || gap < 100 || gap > 3000)))
        return 1;
    return godox_trigger_dryrun_with_gap(gap) != ESP_OK;
}
static void config_print(const stage_ui_t *s) {
    char group[8], id[8];
    if (s->config.group)
        snprintf(group, sizeof(group), "%c", 'A' + s->config.group - 1);
    else
        strcpy(group, "ALL");
    if (s->config.id)
        snprintf(id, sizeof(id), "%u", s->config.id);
    else
        strcpy(id, "OFF");
    printf("Stage CH%02u group=%s ID=%s power=1/%u count=%u interval=%lu ms\n", s->config.ch, group,
           id, 128u >> s->config.power, s->config.count, (unsigned long)s->config.interval_ms);
    puts("Group chooses power configuration (ALL=A..E); TEST broadcasts to matching CH/ID. Optical "
         "feedback unavailable.");
}
static int status_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    stage_ui_t s;
    bool busy;
    stage_app_snapshot(&s, &busy);
    rf_state_t rf = rf_transmitter_state();
    printf("FW %s / IDF %s; busy=%d mode=%d sent=%u last=%s\n", esp_app_get_description()->version,
           esp_app_get_description()->idf_ver, busy, s.mode, s.sent, esp_err_to_name(s.result));
    printf("RF initialized=%d fault=%d last PLAY+cleanup=%lu us\n", rf.initialized, rf.fault,
           (unsigned long)rf.last_window_us);
    printf("Cache ready=%d preparing=%d phase=%u wave=%u group=%u progress=%u/%u; first TEST "
           "dispatch delay=%lu ms\n",
           s.cache_ready, s.preparing, s.prep_phase, s.prep_wave, s.prep_group, s.prep_done,
           s.prep_total, (unsigned long)s.first_test_delay_ms);
    if (s.battery_valid)
        printf("Battery %d mV / ~%d%% (voltage estimate)\n", s.battery_mv, s.battery_percent);
    else
        puts("Battery unavailable (uncalibrated/missing/invalid ADC sample)");
    config_print(&s);
    printf(
        "Page=%d scan=%d channel=%u remaining=%us captures=%lu errors=%lu save=%s targets=%u/32\n",
        s.page, s.scanning, s.scan_ch, s.scan_seconds, (unsigned long)s.scan_captures,
        (unsigned long)s.scan_errors, esp_err_to_name(s.persist_error), s.target_count);
    for (unsigned i = 0; i < s.target_count; i++)
        printf("%u: CH%02u ID%u hits=%u\n", i + 1, s.targets[i].ch, s.targets[i].id,
               s.targets[i].hits);
    return 0;
}
static int rx_dump_cmd(int argc, char **argv) {
    (void)argv;
    if (argc != 1)
        return 1;
    esp_err_t e = stage_app_rx_dump();
    if (e != ESP_OK)
        printf("RXIQ export: %s; see reason above\n", esp_err_to_name(e));
    return e != ESP_OK;
}
static int scan_cmd(int argc, char **argv) {
    unsigned long ch = 0;
    if (argc > 2 || (argc == 2 && (!number(argv[1], &ch) || ch < 1 || ch > 32))) {
        puts("scan [Godox CH 1..32]; omit for sweep");
        return 1;
    }
    return stage_app_scan_channel(ch) != ESP_OK;
}
static int plan_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    stage_ui_t s;
    stage_app_snapshot(&s, NULL);
    config_print(&s);
    uint32_t hz;
    unsigned wifi;
    int32_t offset;
    godox_channel_plan(s.config.ch, &hz, &wifi, &offset);
    printf("Target=%lu Hz, WiFi CH%u offset=%ld Hz; one TEST command per count; plan does not "
           "transmit\n",
           (unsigned long)hz, wifi, (long)offset);
    puts("Sequence: cached prepare, mode/power if context changed, TEST x count, unprepare, TX "
         "stop.");
    puts("Same RF settings reuse Flash wave cache. PHY kept warm for 30s; stop/errors fully "
         "release it.");
    puts("97-bit properties: 398us software gate; 81-bit TEST: 380us gate. New properties/CH/ID "
         "need board validation.");
    return 0;
}
static int prepare_cmd(int argc, char **argv) {
    (void)argv;
    if (argc != 1)
        return 1;
    esp_err_t err = stage_app_prepare();
    printf("Prepare cache: %s; wait until status shows ready\n", esp_err_to_name(err));
    return err != ESP_OK;
}
static int set_cmd(int argc, char **argv) {
    if (argc != 3) {
        puts("set ch|group|id|power|count|interval VALUE");
        return 1;
    }
    stage_ui_t s;
    stage_app_snapshot(&s, NULL);
    stage_config_t c = s.config;
    unsigned long v = 0;
    if (!strcmp(argv[1], "group")) {
        if (!strcasecmp(argv[2], "ALL"))
            c.group = 0;
        else if (strlen(argv[2]) == 1 && argv[2][0] >= 'A' && argv[2][0] <= 'E')
            c.group = argv[2][0] - 'A' + 1;
        else
            return 1;
    } else if (!strcmp(argv[1], "power")) {
        const char *values[] = {"1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"};
        bool found = false;
        for (unsigned i = 0; i < 8; i++)
            if (!strcmp(argv[2], values[i])) {
                c.power = i;
                found = true;
                break;
            }
        if (!found)
            return 1;
    } else if (!strcmp(argv[1], "id") && !strcasecmp(argv[2], "OFF"))
        c.id = 0;
    else {
        if (!number(argv[2], &v))
            return 1;
        if (!strcmp(argv[1], "ch") && v >= 1 && v <= 32)
            c.ch = v;
        else if (!strcmp(argv[1], "id") && v <= 99)
            c.id = v;
        else if (!strcmp(argv[1], "count") && v >= 1 && v <= 999)
            c.count = v;
        else if (!strcmp(argv[1], "interval") && v >= STAGE_INTERVAL_MIN_MS &&
                 v <= STAGE_INTERVAL_MAX_MS)
            c.interval_ms = v;
        else
            return 1;
    }
    esp_err_t e = stage_app_set(&c);
    stage_app_snapshot(&s, NULL);
    printf("Configuration save: %s; cache_ready=%d preparing=%d\n", esp_err_to_name(e),
           s.cache_ready, s.preparing);
    return e != ESP_OK;
}
void app_main(void) {
    ESP_ERROR_CHECK(stage_board_power_init());
    setvbuf(stdout, NULL, _IONBF, 0);
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t size = 0;
    esp_err_t e = esp_flash_get_size(NULL, &size);
    bool flash_ok = e == ESP_OK && size >= 8 * 1024 * 1024;
    printf("Godox-S3-Trigger %s; ESP32-S3 rev %u cores %u; physical flash %lu bytes (%s)\n",
           esp_app_get_description()->version, chip.revision, chip.cores, (unsigned long)size,
           esp_err_to_name(e));
    puts("Boot RF uninitialized/stopped. Touch settings; BOOT starts a finite sequence. Serial "
         "fire retains legacy test.");
    e = nvs_flash_init();
    if (e != ESP_OK)
        ESP_LOGE("main", "NVS init failed: %s (no automatic erase)", esp_err_to_name(e));
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
        ESP_LOGE("main", "Event loop: %s", esp_err_to_name(e));
    ESP_ERROR_CHECK(stage_app_init(flash_ok));
    esp_console_repl_t *repl;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "godox>";
    rc.task_stack_size = 6144;
    rc.task_core_id = 0;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &rc, &repl));
    const esp_console_cmd_t commands[] = {
        {.command = "scan",
         .help = "scan [Godox CH 1..32]: fixed or sweep, max 120s; stop cancels",
         .func = scan_cmd},
        {.command = "rx_dump",
         .help = "Export retained raw IQ after scanning stops",
         .func = rx_dump_cmd},
        {.command = "status",
         .help = "Show stage configuration and last result",
         .func = status_cmd},
        {.command = "plan", .help = "Print stage sequence without RF", .func = plan_cmd},
        {.command = "prepare",
         .help = "Recheck/rebuild wave cache without transmitting",
         .func = prepare_cmd},
        {.command = "set",
         .help = "Set stage ch/group/id/power/count/interval",
         .hint = "FIELD VALUE",
         .func = set_cmd},
        {.command = "stage", .help = "Run finite stage sequence (same as BOOT)", .func = stage_cmd},
        {.command = "dryrun",
         .help = "Legacy test dryrun [test_gap_ms]",
         .hint = "[test_gap_ms]",
         .func = dryrun_cmd},
        {.command = "fire",
         .help = "Legacy two-TEST diagnostic [test_gap_ms]",
         .hint = "[test_gap_ms]",
         .func = fire_cmd},
        {.command = "stop", .help = "Cancel either sequence and close RF", .func = stop_cmd}};
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
#if CONFIG_STAGE_UI_ENABLED
    e = stage_board_start();
    if (e != ESP_OK)
        ESP_LOGE("main", "Touch screen startup: %s; use serial console", esp_err_to_name(e));
#endif
}
