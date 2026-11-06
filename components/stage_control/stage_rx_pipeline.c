/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage_rx_pipeline.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "godox_scan.h"
#include "rf_transmitter.h"
#include <stdatomic.h>
#include <stdlib.h>
enum { SLOTS = 4, WORDS = 16128 };
struct stage_rx_pipeline {
    QueueHandle_t free, full;
    SemaphoreHandle_t done;
    stage_rx_sample_t sample[SLOTS];
    unsigned count, fixed;
    int64_t deadline;
    atomic_bool stopping, finished;
    atomic_uint focus;
    esp_err_t result;
};
static void producer(void *arg) {
    stage_rx_pipeline_t *p = arg;
    unsigned band = p->fixed ? godox_scan_band(p->fixed) : 0, dwell = 0, failures = 0;
    uint32_t captures = 0, errors = 0;
    uint64_t listening = 0, waiting = 0;
    unsigned focus_ch = 0, focus_wifi = 0;
    int64_t focus_end = 0, next_focus = 0;
    esp_task_wdt_add(NULL);
    while (!atomic_load(&p->stopping) && !rf_transmitter_cancelled() &&
           esp_timer_get_time() < p->deadline) {
        unsigned index;
        int64_t wait = esp_timer_get_time();
        BaseType_t got = xQueueReceive(p->free, &index, pdMS_TO_TICKS(10));
        waiting += esp_timer_get_time() - wait;
        esp_task_wdt_reset();
        if (got != pdTRUE)
            continue;
        if (atomic_load(&p->stopping) || rf_transmitter_cancelled() ||
            esp_timer_get_time() >= p->deadline) {
            xQueueSend(p->free, &index, 0);
            break;
        }
        stage_rx_sample_t *s = &p->sample[index];
        int64_t now = esp_timer_get_time();
        /* Keep a request queued throughout the active window and cooldown. */
        unsigned request = now >= next_focus ? atomic_exchange(&p->focus, 0) : 0;
        if (request) {
            focus_ch = request & 63;
            focus_wifi = request >> 6;
            focus_end = now + 900000;
            next_focus = focus_end + 600000;
            ESP_LOGI("rx", "verify CH%02u LO=%uMHz rate=40000000 window=900ms", focus_ch,
                     2412 + (focus_wifi - 1) * 5);
        }
        bool focused = focus_wifi && now < focus_end;
        s->wifi = focused ? focus_wifi : godox_scan_wifi(band);
        s->focus_ch = focused ? focus_ch : p->fixed;
        s->band = focused ? godox_scan_band(focus_ch) : band;
        s->rate_hz = focused ? 40000000 : 16000000;
        s->error = rf_receiver_capture_iq8_rate(s->wifi, s->rate_hz, s->iq, &s->metrics);
        if (s->error == ESP_OK) {
            captures++;
            failures = 0;
            listening += s->metrics.elapsed_us;
        } else {
            errors++;
            failures++;
        }
        s->captures = captures;
        s->errors = errors;
        s->listening_us = listening;
        s->waiting_us = waiting;
        xQueueSend(p->full, &index, portMAX_DELAY);
        if (failures >= 8) {
            p->result = s->error;
            break;
        }
        if (!p->fixed && !focused && ++dwell == 3) {
            band = (band + 1) % GODOX_SCAN_BANDS;
            dwell = 0;
        }
        if ((captures + errors) % 4 == 0)
            vTaskDelay(1);
    }
    esp_err_t released = rf_receiver_release();
    if (p->result == ESP_OK)
        p->result = released;
    esp_task_wdt_delete(NULL);
    atomic_store(&p->finished, true);
    xSemaphoreGive(p->done);
    vTaskDelete(NULL);
}
static void free_pipeline(stage_rx_pipeline_t *p) {
    for (unsigned i = 0; i < SLOTS; i++)
        free(p->sample[i].iq);
    if (p->free)
        vQueueDelete(p->free);
    if (p->full)
        vQueueDelete(p->full);
    if (p->done)
        vSemaphoreDelete(p->done);
    free(p);
}
esp_err_t stage_rx_open(unsigned fixed, int64_t deadline, stage_rx_pipeline_t **out) {
    if (!out || fixed > 32)
        return ESP_ERR_INVALID_ARG;
    *out = NULL;
    stage_rx_pipeline_t *p = calloc(1, sizeof(*p));
    if (!p)
        return ESP_ERR_NO_MEM;
    atomic_init(&p->stopping, false);
    atomic_init(&p->finished, false);
    atomic_init(&p->focus, 0);
    p->fixed = fixed;
    p->deadline = deadline;
    p->sample[0].iq = heap_caps_malloc(WORDS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool external = p->sample[0].iq != NULL;
    unsigned limit = external ? SLOTS : 2;
    for (unsigned i = 0; i < limit; i++) {
        if (external && i)
            p->sample[i].iq = heap_caps_malloc(WORDS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!external)
            p->sample[i].iq = malloc(WORDS * 2);
        if (!p->sample[i].iq)
            break;
        p->sample[i].index = i;
        p->count++;
    }
    if (!p->count) {
        free_pipeline(p);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t e = rf_receiver_init();
    if (e == ESP_ERR_NO_MEM && p->count > 1) {
        for (unsigned i = 1; i < p->count; i++) {
            free(p->sample[i].iq);
            p->sample[i].iq = NULL;
        }
        p->count = 1;
        e = rf_receiver_init();
    }
    if (e != ESP_OK) {
        free_pipeline(p);
        return e;
    }
    p->free = xQueueCreate(p->count, sizeof(unsigned));
    p->full = xQueueCreate(p->count, sizeof(unsigned));
    p->done = xSemaphoreCreateBinary();
    if (!p->free || !p->full || !p->done) {
        rf_receiver_release();
        free_pipeline(p);
        return ESP_ERR_NO_MEM;
    }
    for (unsigned i = 0; i < p->count; i++)
        xQueueSend(p->free, &i, 0);
    if (xTaskCreatePinnedToCore(producer, "iq_capture", 4096, p, 5, NULL, 0) != pdPASS) {
        rf_receiver_release();
        free_pipeline(p);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI("rx", "capture core0, decode core1; IQ8 slots=%u x 32256 bytes in %s", p->count,
             external ? "PSRAM" : "internal SRAM");
    *out = p;
    return ESP_OK;
}
esp_err_t stage_rx_next(stage_rx_pipeline_t *p, const stage_rx_sample_t **out) {
    unsigned index;
    if (xQueueReceive(p->full, &index, pdMS_TO_TICKS(20)) == pdTRUE) {
        *out = &p->sample[index];
        return ESP_OK;
    }
    return atomic_load(&p->finished) ? ESP_ERR_NOT_FOUND : ESP_ERR_TIMEOUT;
}
void stage_rx_recycle(stage_rx_pipeline_t *p, const stage_rx_sample_t *sample) {
    unsigned index = sample->index;
    xQueueSend(p->free, &index, portMAX_DELAY);
}
esp_err_t stage_rx_close(stage_rx_pipeline_t *p) {
    atomic_store(&p->stopping, true);
    xSemaphoreTake(p->done, portMAX_DELAY);
    esp_err_t e = p->result;
    free_pipeline(p);
    return e;
}
void stage_rx_focus(stage_rx_pipeline_t *p, unsigned ch, unsigned wifi) {
    if (ch >= 1 && ch <= 32 && wifi >= 1 && wifi <= 13)
        atomic_store(&p->focus, ch | (wifi << 6));
}
