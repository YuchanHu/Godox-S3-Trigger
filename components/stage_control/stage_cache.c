/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage_cache.h"
#include "stage_workspace.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"
#include "godox_protocol.h"
#include <stdlib.h>
#include <string.h>
static esp_partition_mmap_handle_t mapping;
static const uint8_t *mapped;
static stage_cache_header_t header;
static const esp_partition_t *partition;
void stage_cache_close(void) {
    if (mapped)
        esp_partition_munmap(mapping);
    mapped = NULL;
}
const stage_cache_header_t *stage_cache_header(void) { return mapped ? &header : NULL; }
const uint32_t *stage_cache_words(unsigned i) {
    return mapped && i < header.count ? (const uint32_t *)(mapped + header.entries[i].offset)
                                      : NULL;
}
static esp_err_t find_partition(void) {
    if (!partition)
        partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "wave_cache");
    return partition && partition->size >= STAGE_CACHE_BYTES ? ESP_OK : ESP_ERR_NOT_FOUND;
}
static bool report(stage_cache_progress_t progress, stage_prep_phase_t phase, unsigned done,
                   unsigned total, const stage_cache_entry_t *entry) {
    if (!progress)
        return true;
    stage_prep_wave_t wave = STAGE_PREP_WAVE_NONE;
    unsigned group = 0;
    if (entry) {
        if (entry->type == GODOX_PREPARE)
            wave = STAGE_PREP_WAVE_PREPARE;
        else if (entry->type == GODOX_TEST)
            wave = STAGE_PREP_WAVE_TEST;
        else if (entry->type == GODOX_UNPREPARE)
            wave = STAGE_PREP_WAVE_END;
        else if (entry->key == 0xbc) {
            wave = STAGE_PREP_WAVE_POWER;
            group = entry->group;
        } else
            wave = STAGE_PREP_WAVE_MODE;
    }
    return progress(phase, done, total, wave, group);
}
static esp_err_t open_cache(const stage_config_t *c, stage_cache_progress_t progress,
                            stage_prep_phase_t phase, unsigned base, unsigned total) {
    stage_cache_close();
    stage_cache_header_t plan;
    if (!stage_cache_plan(c, &plan))
        return ESP_ERR_INVALID_ARG;
    if (!total)
        total = plan.count;
    if (!report(progress, phase, base, total, NULL))
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = find_partition();
    if (err != ESP_OK)
        return err;
    err = esp_partition_read(partition, 0, &header, sizeof(header));
    if (err != ESP_OK)
        return err;
    if (!stage_cache_header_valid(&header, c))
        return ESP_ERR_INVALID_CRC;
    const void *mapped_base;
    err = esp_partition_mmap(partition, 0, STAGE_CACHE_BYTES, ESP_PARTITION_MMAP_DATA, &mapped_base,
                             &mapping);
    if (err != ESP_OK)
        return err;
    mapped = mapped_base;
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < header.count; i++) {
        if (!report(progress, phase, base + i, total, &header.entries[i])) {
            stage_cache_close();
            return ESP_ERR_INVALID_STATE;
        }
        hash = stage_cache_hash(hash, mapped + header.entries[i].offset, GODOX_DSP_WORDS * 4);
        esp_task_wdt_reset();
    }
    if (hash != header.data_hash) {
        stage_cache_close();
        return ESP_ERR_INVALID_CRC;
    }
    if (!report(progress, phase, base + header.count, total, NULL)) {
        stage_cache_close();
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
esp_err_t stage_cache_open_report(const stage_config_t *c, stage_cache_progress_t progress) {
    return open_cache(c, progress, STAGE_PREP_CHECK, 0, 0);
}
esp_err_t stage_cache_open(const stage_config_t *c) { return stage_cache_open_report(c, NULL); }
esp_err_t stage_cache_build(const stage_config_t *c, stage_cache_progress_t progress) {
    stage_cache_close();
    esp_err_t err = find_partition();
    if (err != ESP_OK)
        return err;
    if (!stage_cache_plan(c, &header))
        return ESP_ERR_INVALID_ARG;
    uint32_t *wave = stage_wave_workspace();
    size_t end = STAGE_CACHE_HEADER_BYTES + header.count * GODOX_DSP_WORDS * 4;
    size_t erase = (end + 4095) & ~(size_t)4095;
    unsigned done = 0, total = 3 * header.count + 2;
    if (!report(progress, STAGE_PREP_ERASE, done, total, NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_task_wdt_reset();
    err = esp_partition_erase_range(partition, 0, erase);
    esp_task_wdt_reset();
    if (err == ESP_OK)
        done++;
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < header.count && err == ESP_OK; i++) {
        if (!report(progress, STAGE_PREP_COMPUTE, done, total, &header.entries[i])) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        if (!stage_cache_compile(c, &header, i, wave)) {
            err = ESP_FAIL;
            break;
        }
        done++;
        esp_task_wdt_reset();
        hash = stage_cache_hash(hash, wave, GODOX_DSP_WORDS * 4);
        if (!report(progress, STAGE_PREP_WRITE, done, total, &header.entries[i])) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        err = esp_partition_write(partition, header.entries[i].offset, wave, GODOX_DSP_WORDS * 4);
        if (err == ESP_OK)
            done++;
    }
    /* Valid header is written LAST: interrupted builds are never accepted. */
    if (err == ESP_OK && !report(progress, STAGE_PREP_HEADER, done, total, NULL))
        err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        stage_cache_seal(&header, hash);
        err = esp_partition_write(partition, 0, &header, sizeof(header));
        if (err == ESP_OK)
            done++;
    }
    if (err == ESP_OK)
        err = open_cache(c, progress, STAGE_PREP_VERIFY, done, total);
    ESP_LOGI("cache", "build %s: %lu bytes, %lu waves", esp_err_to_name(err), (unsigned long)end,
             (unsigned long)header.count);
    return err;
}
