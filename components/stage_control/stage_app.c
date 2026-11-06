/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage_app.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "godox_protocol.h"
#include "godox_receive.h"
#include "godox_scan.h"
#include "godox_trigger.h"
#include "joint_cache.h"
#include "nvs.h"
#include "rf_receiver.h"
#include "rf_transmitter.h"
#include "stage_cache.h"
#include "stage_rx_pipeline.h"
#include "stage_timing.h"
#include "stage_workspace.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SemaphoreHandle_t lock, modal_ready;
static TaskHandle_t worker;
static stage_ui_t state;
static bool busy, flash_ready, radio_configured;
static bool returning_home;
/* Called under lock after cancellation/cleanup completes. */
static void finish_home_locked(void) {
    if (returning_home && !busy) {
        stage_core_home(&state);
        returning_home = false;
    }
}
static uint32_t legacy_gap, ui_ready_generation;
static int64_t requested_at;
static unsigned requested_scan_channel;
#define RX_EVIDENCE_BLOCKS 16u
#define RX_EVIDENCE_WORDS 1008u
static uint16_t *rx_evidence[RX_EVIDENCE_BLOCKS];
static bool rx_evidence_allocated;
static const char *rx_evidence_reason = "no scan since boot";
static unsigned rx_evidence_wifi, rx_evidence_score;
static unsigned rx_evidence_quality, rx_evidence_rate = 16000000;
static bool rx_evidence_valid, rx_exporting;
static void rx_evidence_free(void) {
    for (unsigned i = 0; i < RX_EVIDENCE_BLOCKS; i++) {
        free(rx_evidence[i]);
        rx_evidence[i] = NULL;
    }
    rx_evidence_allocated = false;
    rx_evidence_valid = false;
}
/* The worker owns this buffer while busy. Export claims busy before reading. */
esp_err_t stage_app_rx_dump(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    bool scanning = state.scanning;
    xSemaphoreGive(lock);
    if (scanning) {
        puts("RXIQ stopping scan before export...");
        stage_app_stop();
        int64_t deadline = esp_timer_get_time() + 5000000;
        do {
            vTaskDelay(pdMS_TO_TICKS(10));
            xSemaphoreTake(lock, portMAX_DELAY);
            scanning = state.scanning;
            xSemaphoreGive(lock);
        } while (scanning && esp_timer_get_time() < deadline);
        if (scanning) {
            puts("RXIQ scan stop timed out; wait for job=4 ended and retry");
            return ESP_ERR_TIMEOUT;
        }
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (busy || returning_home || state.sleep_requested) {
        printf("RXIQ unavailable: worker_busy=%d sleep_requested=%d; retry when idle\n", busy,
               state.sleep_requested);
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (!rx_evidence_valid) {
        printf("RXIQ unavailable: %s; captures=%lu errors=%lu. Start a new scan, then run "
               "rx_dump before entering single control or firing.\n",
               rx_evidence_reason, (unsigned long)state.scan_captures,
               (unsigned long)state.scan_errors);
        xSemaphoreGive(lock);
        return ESP_ERR_NOT_FOUND;
    }
    busy = true;
    rx_exporting = true;
    xSemaphoreGive(lock);
    printf("RXIQ BEGIN rate=%u count=16128 lo=%u score=%u precision=8\n", rx_evidence_rate,
           2412 + (rx_evidence_wifi - 1) * 5, rx_evidence_score);
    for (unsigned i = 0; i < 16128; i += 32) {
        char line[192];
        int used = snprintf(line, sizeof(line), "RXIQ %05u ", i);
        for (unsigned j = 0; j < 32; j++) {
            uint16_t packed = rx_evidence[(i + j) / RX_EVIDENCE_WORDS][(i + j) % RX_EVIDENCE_WORDS];
            int iv = (int8_t)(packed & 255), qv = (int8_t)(packed >> 8);
            uint32_t word = ((unsigned)(iv * 4) & 1023) | (((unsigned)(qv * 4) & 1023) << 10);
            used += snprintf(line + used, sizeof(line) - used, "%05lx", (unsigned long)word);
        }
        puts(line);
    }
    puts("RXIQ END");
    xSemaphoreTake(lock, portMAX_DELAY);
    busy = false;
    rx_exporting = false;
    finish_home_locked();
    xSemaphoreGive(lock);
    return ESP_OK;
}
typedef enum { JOB_PREPARE, JOB_STAGE, JOB_LEGACY, JOB_RELEASE, JOB_SCAN, JOB_JOINT } job_t;
static job_t job;
static const char *TAG = "stage";
static esp_err_t save_config(void) {
    nvs_handle_t h;
    esp_err_t e = nvs_open("godox_stage", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_set_blob(h, "config_v1", &state.config, sizeof(state.config));
    if (e == ESP_OK)
        e = nvs_set_blob(h, "joint_v1", &state.joint, sizeof(state.joint));
    if (e == ESP_OK)
        e = nvs_set_blob(h, "targets_v1", state.targets,
                         state.target_count * sizeof(stage_target_t));
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    return e;
}
void stage_app_snapshot(stage_ui_t *s, bool *b) {
    xSemaphoreTake(lock, portMAX_DELAY);
    *s = state;
    if (b)
        *b = busy;
    xSemaphoreGive(lock);
}
void stage_app_battery_sample(int mv, bool valid) {
    int percent = valid ? stage_battery_percent(mv) : -1;
    xSemaphoreTake(lock, portMAX_DELAY);
    state.battery_mv = valid ? mv : 0;
    if (percent < 0) {
        state.battery_valid = false;
        state.battery_percent = -1;
    } else {
        if (!state.battery_valid || abs(percent - state.battery_percent) >= 2 || percent == 0 ||
            percent == 100)
            state.battery_percent = percent;
        state.battery_valid = true;
    }
    xSemaphoreGive(lock);
}
void stage_app_displayed(const stage_ui_t *s) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (s->prep_generation == state.prep_generation) {
        if (s->preparing && state.preparing)
            xSemaphoreGive(modal_ready);
        else if (!s->preparing && s->cache_ready && state.cache_ready)
            ui_ready_generation = state.prep_generation;
    }
    xSemaphoreGive(lock);
}
static void prepare_locked(void) {
    stage_cache_header_t plan;
    stage_cache_plan(&state.config, &plan);
    busy = true;
    job = JOB_PREPARE;
    state.preparing = true;
    state.joint_active = false;
    state.cache_ready = false;
    state.prep_done = 0;
    state.prep_total = plan.count;
    state.prep_phase = STAGE_PREP_CHECK;
    state.prep_wave = STAGE_PREP_WAVE_NONE;
    state.prep_group = 0;
    state.prep_generation++;
    state.mode = STAGE_READY;
    state.legacy = false;
    state.field = -1;
    state.result = 0;
    ui_ready_generation = UINT32_MAX;
    while (xSemaphoreTake(modal_ready, 0) == pdTRUE) {
    }
    rf_transmitter_clear_stop();
    xTaskNotifyGive(worker);
}
esp_err_t stage_app_prepare(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (busy || returning_home || state.sleep_requested || state.field >= 0) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    prepare_locked();
    xSemaphoreGive(lock);
    return ESP_OK;
}
static bool wait_until(int64_t end) {
    while (!rf_transmitter_cancelled() && esp_timer_get_time() < end) {
        esp_task_wdt_reset();
        vTaskDelay(1);
    }
    return !rf_transmitter_cancelled();
}
static int progress(stage_prep_phase_t phase, unsigned done, unsigned total, stage_prep_wave_t wave,
                    unsigned group) {
    xSemaphoreTake(lock, portMAX_DELAY);
    state.prep_done = done;
    state.prep_total = total;
    state.prep_phase = phase;
    state.prep_wave = wave;
    state.prep_group = group;
    xSemaphoreGive(lock);
    esp_task_wdt_reset();
    return !rf_transmitter_cancelled();
}
static esp_err_t prepare_cache(const stage_config_t *c) {
    radio_configured = false;
    esp_err_t err = rf_transmitter_release();
    if (err != ESP_OK)
        return err;
    int64_t start = esp_timer_get_time();
    err = stage_cache_open_report(c, progress);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "wave cache hit; no waveform calculation/flash erase");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "wave cache miss: %s; RF stopped, waiting for preparation modal",
             esp_err_to_name(err));
    stage_cache_header_t plan;
    stage_cache_plan(c, &plan);
    if (!progress(STAGE_PREP_START, 0, 3 * plan.count + 2, STAGE_PREP_WAVE_NONE, 0))
        return ESP_ERR_INVALID_STATE;
#if CONFIG_STAGE_UI_ENABLED
    if (xSemaphoreTake(modal_ready, pdMS_TO_TICKS(2500)) != pdTRUE)
        ESP_LOGW(TAG, "display acknowledgement timeout; inputs remain locked, continuing offline "
                      "preparation");
#endif
    if (rf_transmitter_cancelled())
        return ESP_ERR_INVALID_STATE;
    err = stage_cache_build(c, progress);
    ESP_LOGI(TAG, "preparation complete: %s, %lld ms", esp_err_to_name(err),
             (esp_timer_get_time() - start) / 1000);
    return err;
}
static esp_err_t send_cached(unsigned index) {
    const stage_cache_header_t *h = stage_cache_header();
    if (!h || index >= h->count)
        return ESP_ERR_INVALID_STATE;
    return rf_transmitter_send_window(stage_cache_words(index), GODOX_DSP_WORDS,
                                      h->entries[index].window_us);
}
static esp_err_t run_stage(const stage_config_t *c) {
    const stage_cache_header_t *h = stage_cache_header();
    if (!h || !stage_cache_header_valid(h, c))
        return ESP_ERR_INVALID_STATE;
    uint32_t hz;
    unsigned wifi;
    int32_t offset;
    if (!godox_channel_plan(c->ch, &hz, &wifi, &offset))
        return ESP_ERR_INVALID_ARG;
    bool warm = rf_transmitter_state().initialized;
    if (!warm)
        radio_configured = false;
    ESP_LOGI(TAG,
             "cached stage: CH%02u %lu Hz, group=%u ID=%u 1/%u count=%u interval=%lu ms, warm=%d "
             "configured=%d",
             c->ch, (unsigned long)hz, c->group, c->id, 128u >> c->power, c->count,
             (unsigned long)c->interval_ms, warm, radio_configured);
    esp_err_t err = rf_transmitter_init_channel(wifi);
    if (err != ESP_OK)
        goto stopped;
    err = send_cached(0); // prepare
    if (err != ESP_OK)
        goto stopped;
    if (!wait_until(esp_timer_get_time() + 20000)) {
        err = ESP_ERR_INVALID_STATE;
        goto stopped;
    }
    if (!radio_configured) {
        for (unsigned i = 1; i < h->count - 2 && err == ESP_OK; i++) {
            err = send_cached(i);
            if (err == ESP_OK && !wait_until(esp_timer_get_time() + 20000))
                err = ESP_ERR_INVALID_STATE;
        }
        if (err != ESP_OK)
            goto stopped;
        radio_configured = true;
    }
    int64_t deadline = esp_timer_get_time();
    for (unsigned n = 0; n < c->count; n++) {
        if (!wait_until(deadline)) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        int64_t begin = esp_timer_get_time();
        err = send_cached(h->count - 2);
        if (err != ESP_OK)
            break;
        bool limited = n > 0 && begin - deadline > 1000; // One scheduler tick of tolerance.
        xSemaphoreTake(lock, portMAX_DELAY);
        state.sent = n + 1;
        if (n == 0)
            state.first_test_delay_ms = (begin - requested_at) / 1000;
        xSemaphoreGive(lock);
        if (n == 0)
            ESP_LOGI(TAG, "BOOT/command to first TEST dispatch=%lld ms",
                     (begin - requested_at) / 1000);
        if (n == 0 || n + 1 == c->count)
            ESP_LOGI(TAG, "TEST %u/%u dispatched at %lld us", n + 1, c->count, begin);
        else
            ESP_LOGD(TAG, "TEST %u/%u dispatched at %lld us", n + 1, c->count, begin);
        esp_task_wdt_reset();
        // A short/overrun sequence can otherwise remain runnable continuously.
        // Let CPU1 idle and service its watchdog even when no deadline wait is needed.
        if ((n + 1) % 16 == 0 && n + 1 < c->count)
            vTaskDelay(1);
        deadline = stage_next_test_deadline(begin, esp_timer_get_time(), c->interval_ms, &limited);
        if (limited) {
            xSemaphoreTake(lock, portMAX_DELAY);
            state.timing_limited = true;
            xSemaphoreGive(lock);
        }
    }
    if (err == ESP_OK && !rf_transmitter_cancelled()) {
        if (wait_until(esp_timer_get_time() + 20000))
            err = send_cached(h->count - 1);
        else
            err = ESP_ERR_INVALID_STATE;
    }
stopped:;
    esp_err_t off = rf_transmitter_stop();
    if (err == ESP_OK)
        err = off;
    if (err != ESP_OK || rf_transmitter_cancelled()) {
        radio_configured = false;
        esp_err_t released = rf_transmitter_release();
        if (err == ESP_OK)
            err = released;
    }
    return err;
}
static void release_radio(void) {
    esp_task_wdt_add(NULL);
    radio_configured = false;
    esp_err_t e = rf_transmitter_release();
    esp_task_wdt_delete(NULL);
    if (e != ESP_OK)
        ESP_LOGE(TAG, "idle RF release: %s", esp_err_to_name(e));
}
static esp_err_t scan_worker(void) {
    radio_configured = false;
    stage_cache_close();
    joint_cache_close();
    esp_err_t e = rf_transmitter_release();
    if (e != ESP_OK)
        return e;
    rx_evidence_free();
    rx_evidence_reason = "scan has no successful capture";
    rx_evidence_score = rx_evidence_quality = 0;
    godox_scan_init();
    int64_t begin = esp_timer_get_time(), end = begin + 120000000;
    unsigned fixed = requested_scan_channel;
    stage_rx_pipeline_t *pipeline = NULL;
    e = stage_rx_open(fixed, end, &pipeline);
    if (e != ESP_OK)
        return e;
    for (unsigned i = 0; i < RX_EVIDENCE_BLOCKS; i++) {
        rx_evidence[i] = malloc(RX_EVIDENCE_WORDS * sizeof(uint16_t));
        if (!rx_evidence[i]) {
            rx_evidence_free();
            rx_evidence_reason = "IQ evidence allocation failed; capture queue has priority";
            ESP_LOGW("rx", "%s", rx_evidence_reason);
            break;
        }
    }
    rx_evidence_allocated = rx_evidence[RX_EVIDENCE_BLOCKS - 1] != NULL;
    unsigned visits[GODOX_SCAN_BANDS] = {0};
    godox_scan_confirmation_t confirmation = {0};
    uint32_t channel_rejected = 0;
    uint32_t captures = 0, errors = 0, trials = 0, sync = 0, frames = 0, rejected = 0, tested = 0;
    int64_t next_log = 0;
    uint32_t next_verify_ms = 0, verify_captures = 0, verify_headers = 0;
    ESP_LOGI("rx", "start %s; six bands cover CH1..32, FFT gate + rotating fallback",
             fixed ? "fixed CH" : "all-CH sweep");
    while (!rf_transmitter_cancelled() && esp_timer_get_time() < end) {
        uint32_t scan_ms = (uint32_t)((esp_timer_get_time() - begin) / 1000);
        if (scan_ms >= next_verify_ms) {
            unsigned pending = godox_scan_pending(&confirmation, scan_ms);
            if (pending) {
                stage_rx_focus(pipeline, pending, godox_scan_wifi(godox_scan_band(pending)));
                next_verify_ms = scan_ms + 1500;
            }
        }
        const stage_rx_sample_t *sample;
        e = stage_rx_next(pipeline, &sample);
        esp_task_wdt_reset();
        if (e == ESP_ERR_NOT_FOUND)
            break;
        if (e == ESP_ERR_TIMEOUT)
            continue;
        int64_t process_begin = esp_timer_get_time();
        captures = sample->captures;
        errors = sample->errors;
        unsigned ch = sample->band * 6 + 1, best_sync = 0, decoded = 0, protocol_frames = 0;
        bool changed = false;
        if (sample->error == ESP_OK) {
            godox_scan_candidate_t candidates[GODOX_SCAN_CANDIDATES];
            unsigned n;
            if (sample->rate_hz == 40000000) {
                verify_captures++;
                n = 1;
                candidates[0] = (godox_scan_candidate_t){
                    sample->focus_ch, godox_scan_offset(sample->focus_ch, 0, sample->wifi), 9};
                int32_t other = godox_scan_offset(sample->focus_ch, 1, sample->wifi);
                if (other != candidates[0].offset)
                    candidates[n++] = (godox_scan_candidate_t){sample->focus_ch, other, 9};
            } else
                n = godox_scan_detect_wifi(sample->iq, 16128, sample->wifi, candidates);
            unsigned visit = visits[sample->band]++;
            uint32_t selected = 0, seen[32][4] = {{0}};
            unsigned selected_count = 0;
            for (unsigned k = 0; k < n; k++) {
                godox_scan_candidate_t *c = &candidates[k];
                if (sample->focus_ch && c->ch != sample->focus_ch)
                    continue;
                uint32_t mask = 1u << (c->ch - 1);
                /* Check one rotating low-score candidate instead of blocking
                 * the capture queue with a50ms all-candidate pass. */
                bool fallback = (visit % 4) == 0 && k == (visit / 4) % n;
                if (!fallback && !sample->focus_ch) {
                    if (c->score < 8)
                        continue;
                    if (!(selected & mask)) {
                        if (selected_count >= 2)
                            continue;
                        selected |= mask;
                        selected_count++;
                    }
                }
                ch = c->ch;
                tested++;
                unsigned ids[4];
                int32_t carriers[4];
                godox_rx_diagnostics_t d;
                unsigned found;
                if (sample->rate_hz == 40000000) {
                    found = godox_verify_iq8_header(sample->iq, 16128, c->offset,
                                                   ids, 4, &d, carriers);
                    verify_headers += found;
                } else
                    found = godox_decode_iq8_rate(sample->iq, 16128, sample->rate_hz,
                                                 c->offset, ids, 4, &d, carriers);
                trials += d.trials;
                sync += d.sync;
                protocol_frames += d.frames;
                if (sample->rate_hz == 16000000)
                    frames += d.frames;
                rejected += d.carrier_rejected;
                if (d.best_sync_bits > best_sync)
                    best_sync = d.best_sync_bits;
                for (unsigned i = 0; i < found; i++) {
                    unsigned id = ids[i];
                    if (seen[ch - 1][id / 32] & (1u << (id % 32)))
                        continue;
                    decoded++;
                    unsigned located = godox_scan_locate(sample->wifi, carriers[i]);
                    if (located != ch) {
                        channel_rejected++;
                        continue;
                    }
                    seen[ch - 1][id / 32] |= 1u << (id % 32);
                    int confirmed = godox_scan_confirm(&confirmation, ch, id, sample->rate_hz,
                                                       (uint32_t)((esp_timer_get_time() - begin) / 1000));
                    if (confirmed != 1) {
                        if (confirmed < 0) {
                            ESP_LOGI("rx",
                                     "candidate CH%02u ID%u native=%ldHz LO=%uMHz; queued verification",
                                     ch, id, (long)carriers[i], 2412 + (sample->wifi - 1) * 5);
                        }
                        continue;
                    }
                    ESP_LOGI("rx", "confirmed CH%02u ID%u native=%ldHz LO=%uMHz", ch, id,
                             (long)carriers[i], 2412 + (sample->wifi - 1) * 5);
                    xSemaphoreTake(lock, portMAX_DELAY);
                    if (stage_target_add(&state, ch, id)) {
                        state.list_page = (state.target_count - 1) / 4;
                        changed = true;
                    }
                    xSemaphoreGive(lock);
                }
            }
            if (changed) {
                xSemaphoreTake(lock, portMAX_DELAY);
                state.persist_error = save_config();
                xSemaphoreGive(lock);
            }
            /* Retain the verification failure itself once discovery succeeded.
             * Otherwise a valid16MS/s frame hides every failed40MS/s capture. */
            unsigned quality = best_sync + (decoded ? 64 : 0) + (protocol_frames ? 32 : 0) +
                               (sample->rate_hz == 40000000 ? 128 : 0);
            if (rx_evidence_allocated && (!rx_evidence_valid || quality > rx_evidence_quality ||
                                          (decoded && quality == rx_evidence_quality))) {
                for (unsigned k = 0; k < 16128; k++) {
                    uint16_t word = (uint8_t)sample->iq[2 * k] |
                                    ((uint16_t)(uint8_t)sample->iq[2 * k + 1] << 8);
                    rx_evidence[k / RX_EVIDENCE_WORDS][k % RX_EVIDENCE_WORDS] = word;
                }
                rx_evidence_wifi = sample->wifi;
                rx_evidence_rate = sample->rate_hz;
                rx_evidence_score = best_sync;
                rx_evidence_quality = quality;
                rx_evidence_valid = true;
            }
        }
        int64_t now = esp_timer_get_time();
        if (now >= next_log) {
            ESP_LOGI("rx",
                     "band=%u CH%02u LO=%uMHz captures=%lu errors=%lu tested=%lu "
                     "trials=%lu sync=%lu frames=%lu carrier_reject=%lu decode=%lldus "
                     "coverage=%llupermille backpressure=%lluus channel_reject=%lu rate=%u "
                     "cap=%luus verify=%lu headers=%lu clip=%lu peak=%lu IQ8",
                     sample->band, ch, 2412 + (sample->wifi - 1) * 5, (unsigned long)captures,
                     (unsigned long)errors, (unsigned long)tested, (unsigned long)trials,
                     (unsigned long)sync, (unsigned long)frames, (unsigned long)rejected,
                     (long long)(now - process_begin),
                     (unsigned long long)(sample->listening_us * 1000 / (now - begin)),
                     (unsigned long long)sample->waiting_us, (unsigned long)channel_rejected,
                     sample->rate_hz, (unsigned long)sample->metrics.elapsed_us,
                     (unsigned long)verify_captures, (unsigned long)verify_headers,
                     (unsigned long)sample->metrics.clipped, (unsigned long)sample->metrics.peak);
            next_log = now + 2000000;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        state.scan_ch = ch;
        state.scan_captures = captures;
        state.scan_errors = errors;
        int64_t remaining = end - now;
        state.scan_seconds = remaining > 0 ? (remaining + 999999) / 1000000 : 0;
        xSemaphoreGive(lock);
        stage_rx_recycle(pipeline, sample);
        vTaskDelay(1); // Consumer yields independently while core0 captures.
    }
    e = stage_rx_close(pipeline); // Joins capture owner before any TX can reuse RF SRAM.
    xSemaphoreTake(lock, portMAX_DELAY);
    state.persist_error = save_config();
    xSemaphoreGive(lock);
    ESP_LOGI("rx", "scan ended: captures=%lu errors=%lu verify=%lu headers=%lu IQ=%s", (unsigned long)captures,
             (unsigned long)errors, (unsigned long)verify_captures, (unsigned long)verify_headers,
             rx_evidence_valid ? "retained (8-bit expanded)" : rx_evidence_reason);
    return e;
}
static int joint_progress(unsigned done, unsigned total) {
    return progress(STAGE_PREP_COMPUTE, done, total, STAGE_PREP_WAVE_TEST, 0);
}
static esp_err_t joint_control(const stage_config_t *c, unsigned type, unsigned group, unsigned key,
                               unsigned value, uint32_t *work) {
    uint8_t raw[12], bits[97];
    uint32_t hz, gate;
    unsigned wifi;
    int32_t offset;
    if (rf_transmitter_cancelled())
        return ESP_ERR_INVALID_STATE;
    if (!godox_channel_plan(c->ch, &hz, &wifi, &offset))
        return ESP_ERR_INVALID_ARG;
    /* PHY calibration can touch dump SRAM. Finish it before generating there. */
    esp_err_t e = rf_transmitter_init_channel(wifi);
    if (e != ESP_OK)
        return e;
    bool good = type == 0xa9 ? godox_property_frame(c->id, group, key, value, raw, bits)
                             : godox_short_frame(c->id, type, raw, bits);
    if (!good || !godox_dsp_generate(bits, type == 0xa9 ? 97 : 81, offset, work, &gate))
        return ESP_FAIL;
    e = rf_transmitter_send_window(work, GODOX_DSP_WORDS, gate);
    esp_task_wdt_reset();
    return e;
}
static esp_err_t joint_worker(const stage_config_t *base) {
    stage_target_t targets[STAGE_TARGET_MAX];
    unsigned count;
    xSemaphoreTake(lock, portMAX_DELAY);
    count = state.target_count;
    for (unsigned i = 0; i < count; i++)
        targets[i] = state.targets[i];
    state.preparing = true;
    state.prep_phase = STAGE_PREP_COMPUTE;
    xSemaphoreGive(lock);
    stage_cache_close();
    radio_configured = false;
    esp_err_t e = rf_transmitter_release();
    uint32_t *work = stage_wave_workspace();
    if (e == ESP_OK)
        e = joint_cache_prepare(targets, count, work, joint_progress);
    for (unsigned i = 0; i < count && e == ESP_OK; i++) {
        stage_config_t c = *base;
        c.ch = targets[i].ch;
        c.id = targets[i].id;
        if (!progress(STAGE_PREP_WRITE, i, count, STAGE_PREP_WAVE_MODE, 0)) {
            e = ESP_ERR_INVALID_STATE;
            break;
        }
        e = joint_control(&c, GODOX_PREPARE, 0, 0, 0, work);
        if (e == ESP_OK && !wait_until(esp_timer_get_time() + 20000))
            e = ESP_ERR_INVALID_STATE;
        if (e == ESP_OK)
            e = joint_control(&c, 0xa9, c.group, 0xb1, 1, work);
        if (e == ESP_OK && !wait_until(esp_timer_get_time() + 20000))
            e = ESP_ERR_INVALID_STATE;
        unsigned first = c.group ? c.group : 1, last = c.group ? c.group : 5;
        for (unsigned g = first; g <= last && e == ESP_OK; g++) {
            e = joint_control(&c, 0xa9, g, 0xbc, (7 - c.power) * 10, work);
            if (e == ESP_OK && !wait_until(esp_timer_get_time() + 20000))
                e = ESP_ERR_INVALID_STATE;
        }
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    state.preparing = false;
    xSemaphoreGive(lock);
    int64_t deadline = esp_timer_get_time();
    for (unsigned round = 0; round < base->count && e == ESP_OK; round++) {
        if (!wait_until(deadline)) {
            e = ESP_ERR_INVALID_STATE;
            break;
        }
        int64_t begin = esp_timer_get_time();
        for (unsigned i = 0; i < count && e == ESP_OK; i++) {
            uint32_t hz;
            unsigned wifi;
            int32_t offset;
            godox_channel_plan(targets[i].ch, &hz, &wifi, &offset);
            e = rf_transmitter_init_channel(wifi);
            if (e == ESP_OK)
                e = rf_transmitter_send_window(joint_cache_wave(i), GODOX_DSP_WORDS, 380);
            esp_task_wdt_reset();
            if (rf_transmitter_cancelled()) {
                e = ESP_ERR_INVALID_STATE;
                break;
            }
        }
        bool limited = false;
        if ((round + 1) % 16 == 0)
            vTaskDelay(1);
        deadline =
            stage_next_test_deadline(begin, esp_timer_get_time(), base->interval_ms, &limited);
        xSemaphoreTake(lock, portMAX_DELAY);
        if (e == ESP_OK)
            state.sent = round + 1;
        state.timing_limited |= limited;
        if (!round)
            state.first_test_delay_ms = (begin - requested_at) / 1000;
        xSemaphoreGive(lock);
    }
    if (e == ESP_OK)
        for (unsigned i = 0; i < count && e == ESP_OK; i++) {
            stage_config_t c = *base;
            c.ch = targets[i].ch;
            c.id = targets[i].id;
            e = joint_control(&c, GODOX_UNPREPARE, 0, 0, 0, work);
        }
    joint_cache_close();
    esp_err_t off = rf_transmitter_release();
    return e == ESP_OK ? off : e;
}
static void worker_task(void *arg) {
    (void)arg;
    for (;;) {
        if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000))) {
            if (rf_transmitter_state().initialized) {
                release_radio();
                ESP_LOGI(TAG, "idle 30s: PHY released; wave cache retained");
            }
            continue;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        job_t current = job;
        stage_config_t c = current == JOB_PREPARE ? state.config : state.run;
        uint32_t gap = legacy_gap;
        xSemaphoreGive(lock);
        if (current == JOB_RELEASE) {
            release_radio();
            xSemaphoreTake(lock, portMAX_DELAY);
            busy = false;
            finish_home_locked();
            xSemaphoreGive(lock);
            continue;
        }
        esp_task_wdt_add(NULL);
        if (current != JOB_SCAN) {
            rx_evidence_free();
            rx_evidence_reason = "IQ buffer released for waveform preparation or transmission";
        }
        esp_err_t err;
        if (current == JOB_PREPARE)
            err = prepare_cache(&c);
        else if (current == JOB_SCAN)
            err = scan_worker();
        else if (current == JOB_JOINT)
            err = joint_worker(&c);
        else if (current == JOB_LEGACY) {
            radio_configured = false;
            err = rf_transmitter_release();
            if (err == ESP_OK)
                err = godox_trigger_fire_test_with_gap(gap);
        } else
            err = run_stage(&c);
        bool cancelled = rf_transmitter_cancelled();
        if (cancelled && err == ESP_OK && current != JOB_SCAN)
            err = ESP_ERR_INVALID_STATE;
        esp_task_wdt_delete(NULL);
        xSemaphoreTake(lock, portMAX_DELAY);
        state.result = err;
        busy = false;
        if (current == JOB_SCAN) {
            state.scanning = false;
            state.scan_seconds = 0;
            state.mode = STAGE_READY;
        } else if (current == JOB_PREPARE) {
            state.preparing = false;
            state.cache_ready = err == ESP_OK;
            state.mode = err == ESP_OK ? STAGE_READY : STAGE_ERROR;
        } else {
            state.preparing = false;
            state.mode =
                rf_transmitter_state().fault
                    ? STAGE_ERROR
                    : (cancelled ? STAGE_STOPPED : (err == ESP_OK ? STAGE_DONE : STAGE_ERROR));
        }
        finish_home_locked();
        xSemaphoreGive(lock);
        ESP_LOGI(
            TAG,
            "job=%d ended: %s; RF TX stopped, PHY initialized=%d; optical feedback unavailable",
            current, esp_err_to_name(err), rf_transmitter_state().initialized);
    }
}
static esp_err_t queue_job(bool old, uint32_t gap) {
    xSemaphoreTake(lock, portMAX_DELAY);
    bool joint = !old && state.page == STAGE_PAGE_JOINT;
    if (busy || returning_home || state.sleep_requested || state.preparing || state.field >= 0 || !flash_ready ||
        rf_transmitter_state().fault || (!old && !joint && !state.cache_ready) ||
        (joint && !state.target_count)) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    state.run = joint ? state.joint : state.config;
    state.joint_active = joint;
    if (joint)
        state.cache_ready = false;
    state.sent = 0;
    state.result = 0;
    state.timing_limited = false;
    state.mode = STAGE_RUNNING;
    state.legacy = old;
    state.first_test_delay_ms = 0;
    busy = true;
    job = old ? JOB_LEGACY : joint ? JOB_JOINT : JOB_STAGE;
    legacy_gap = gap;
    requested_at = esp_timer_get_time();
    rf_transmitter_clear_stop();
    xTaskNotifyGive(worker);
    xSemaphoreGive(lock);
    return ESP_OK;
}
esp_err_t stage_app_start(void) { return queue_job(false, 0); }
esp_err_t stage_app_scan(void) { return stage_app_scan_channel(0); }
esp_err_t stage_app_scan_channel(unsigned channel) {
    if (channel > 32)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (busy || returning_home || state.sleep_requested || state.field >= 0) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    busy = true;
    job = JOB_SCAN;
    requested_scan_channel = channel;
    state.scanning = true;
    state.cache_ready = false;
    state.scan_captures = state.scan_errors = state.persist_error = 0;
    state.scan_seconds = 120;
    state.scan_ch = channel ? channel : 1;
    state.page = STAGE_PAGE_TARGETS;
    state.mode = STAGE_READY;
    rf_transmitter_clear_stop();
    xTaskNotifyGive(worker);
    xSemaphoreGive(lock);
    return ESP_OK;
}
esp_err_t stage_app_clear_targets(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (busy || returning_home || state.sleep_requested) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    stage_target_t old[32];
    memcpy(old, state.targets, sizeof(old));
    unsigned count = state.target_count;
    state.target_count = 0;
    state.list_page = 0;
    memset(state.targets, 0, sizeof(state.targets));
    esp_err_t e = save_config();
    if (e != ESP_OK) {
        memcpy(state.targets, old, sizeof(old));
        state.target_count = count;
    }
    xSemaphoreGive(lock);
    return e;
}
static void stop_locked(void) {
    rf_transmitter_request_stop();
    if (!busy && rf_transmitter_state().initialized) {
        busy = true;
        job = JOB_RELEASE;
        xTaskNotifyGive(worker);
    }
}
void stage_app_shutdown(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    state.sleep_requested = true;
    stop_locked();
    xSemaphoreGive(lock);
}
void stage_app_power(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    stage_action_t action = returning_home ? STAGE_NO_ACTION : stage_core_power(&state);
    if (action == STAGE_SLEEP)
        state.sleep_requested = true;
    else if (action == STAGE_RETURN_HOME)
        returning_home = true;
    if (action != STAGE_NO_ACTION)
        stop_locked();
    finish_home_locked();
    xSemaphoreGive(lock);
}
bool stage_app_quiescent(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    bool idle = !busy;
    xSemaphoreGive(lock);
    return idle && !rf_transmitter_state().initialized;
}
esp_err_t stage_app_legacy_fire(uint32_t gap) { return queue_job(true, gap); }
void stage_app_stop(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    stop_locked();
    xSemaphoreGive(lock);
}
void stage_app_boot(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (rx_exporting || returning_home) {
        xSemaphoreGive(lock);
        return;
    }
    bool preparing = state.preparing, need_prepare = state.page == STAGE_PAGE_SINGLE &&
                                                     !state.cache_ready && state.field < 0 && !busy;
    bool ui_wait = state.page == STAGE_PAGE_SINGLE && state.cache_ready &&
                   ui_ready_generation != state.prep_generation && state.mode != STAGE_RUNNING;
    stage_action_t a = stage_core_boot(&state);
    xSemaphoreGive(lock);
    if (preparing || ui_wait) {
        ESP_LOGI(TAG, "BOOT ignored until preparation and parameter-page refresh complete");
        return;
    }
    if (need_prepare) {
        stage_app_prepare();
        return;
    }
    if (a == STAGE_START) {
        esp_err_t e = stage_app_start();
        if (e != ESP_OK)
            ESP_LOGW(TAG, "BOOT start refused: %s", esp_err_to_name(e));
    } else if (a == STAGE_CANCEL_RUN)
        stage_app_stop();
}
void stage_app_touch(int x, int y) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (rx_exporting || returning_home || state.preparing || (busy && state.page == STAGE_PAGE_MANUAL) ||
        (state.page == STAGE_PAGE_SINGLE && state.cache_ready &&
         ui_ready_generation != state.prep_generation && state.mode != STAGE_RUNNING)) {
        xSemaphoreGive(lock);
        return;
    }
    stage_config_t old = *stage_edit_config(&state);
    int old_page = state.page;
    stage_action_t a = stage_core_touch(&state, x, y);
    if (a == STAGE_ADD_TARGET && !busy) {
        unsigned count = state.target_count;
        unsigned page = state.list_page;
        if (stage_manual_add(&state)) {
            state.persist_error = save_config();
            if (state.persist_error != ESP_OK) {
                state.target_count = count;
                state.targets[count] = (stage_target_t){0};
                state.list_page = page;
                state.manual_status = 4;
            }
        }
    }
    if (a == STAGE_SAVE) {
        esp_err_t e = save_config();
        if (e != ESP_OK) {
            *stage_edit_config(&state) = old;
            state.result = e;
            state.mode = STAGE_ERROR;
        } else if (state.page == STAGE_PAGE_SINGLE &&
                   (!stage_cache_same_rf(&old, &state.config) || !state.cache_ready))
            prepare_locked();
    }
    if (old_page != state.page && state.page == STAGE_PAGE_SINGLE && !state.cache_ready && !busy)
        prepare_locked();
    xSemaphoreGive(lock);
    if (a == STAGE_CANCEL_RUN)
        stage_app_stop();
    else if (a == STAGE_SCAN)
        stage_app_scan();
    else if (a == STAGE_CLEAR_TARGETS)
        stage_app_clear_targets();
}
esp_err_t stage_app_set(const stage_config_t *c) {
    if (!stage_config_valid(c))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (busy || returning_home || state.sleep_requested || state.field >= 0) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    stage_config_t old = state.config;
    state.config = *c;
    esp_err_t e = save_config();
    if (e != ESP_OK)
        state.config = old;
    else if (!stage_cache_same_rf(&old, c) || !state.cache_ready)
        prepare_locked();
    xSemaphoreGive(lock);
    return e;
}
esp_err_t stage_app_init(bool flash_ok) {
    flash_ready = flash_ok;
    stage_core_init(&state);
    state.page = STAGE_PAGE_HOME;
    lock = xSemaphoreCreateMutex();
    modal_ready = xSemaphoreCreateBinary();
    if (!lock || !modal_ready)
        return ESP_ERR_NO_MEM;
    nvs_handle_t h;
    if (nvs_open("godox_stage", NVS_READONLY, &h) == ESP_OK) {
        stage_config_t c;
        size_t size = sizeof(c);
        if (nvs_get_blob(h, "config_v1", &c, &size) == ESP_OK && size == sizeof(c) &&
            stage_config_valid(&c))
            state.config = c;
        size = sizeof(c);
        if (nvs_get_blob(h, "joint_v1", &c, &size) == ESP_OK && size == sizeof(c) &&
            stage_config_valid(&c))
            state.joint = c;
        size = sizeof(state.targets);
        stage_target_t loaded[32];
        if (nvs_get_blob(h, "targets_v1", loaded, &size) == ESP_OK &&
            size % sizeof(stage_target_t) == 0 && size <= sizeof(loaded))
            for (unsigned i = 0; i < size / sizeof(stage_target_t); i++)
                if (stage_target_add(&state, loaded[i].ch, loaded[i].id))
                    state.targets[state.target_count - 1].hits = loaded[i].hits;
        nvs_close(h);
    }
    if (xTaskCreatePinnedToCore(worker_task, "rf_worker", 32768, NULL, 5, &worker, 1) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
