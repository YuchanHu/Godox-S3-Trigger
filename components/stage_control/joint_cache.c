/* SPDX-License-Identifier: GPL-3.0-or-later; cached TEST waves, one per CH/ID. */
#include "joint_cache.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"
#include "godox_protocol.h"
#include "stage_cache_plan.h"
#include <string.h>
typedef struct {
    uint32_t magic, version, count, hash, seal;
    stage_target_t targets[32];
} joint_header_t;
static const uint8_t *mapped;
static esp_partition_mmap_handle_t mapping;
static unsigned mapped_count;
void joint_cache_close(void) {
    if (mapped)
        esp_partition_munmap(mapping);
    mapped = NULL;
    mapped_count = 0;
}
const uint32_t *joint_cache_wave(unsigned i) {
    return mapped && i < mapped_count ? (const uint32_t *)(mapped + 4096 + i * GODOX_DSP_WORDS * 4)
                                      : NULL;
}
static uint32_t seal(joint_header_t h) {
    h.seal = 0;
    return stage_cache_hash(2166136261u, &h, sizeof(h));
}
static esp_err_t open_joint(const esp_partition_t *p, joint_header_t *h) {
    const void *base;
    esp_err_t e = esp_partition_mmap(p, 0, 0x200000, ESP_PARTITION_MMAP_DATA, &base, &mapping);
    if (e != ESP_OK)
        return e;
    mapped = base;
    mapped_count = h->count;
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < h->count; i++) {
        hash = stage_cache_hash(hash, joint_cache_wave(i), GODOX_DSP_WORDS * 4);
        esp_task_wdt_reset();
    }
    if (hash != h->hash) {
        joint_cache_close();
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}
esp_err_t joint_cache_prepare(const stage_target_t *t, unsigned n, uint32_t *work,
                              int (*progress)(unsigned, unsigned)) {
    joint_cache_close();
    if (!t || !work || !n || n > 32)
        return ESP_ERR_INVALID_ARG;
    joint_header_t h = {.magic = 0x4a4f494e, .version = 1, .count = n}, old;
    for (unsigned i = 0; i < n; i++) {
        if (t[i].ch < 1 || t[i].ch > 32 || t[i].id > 99)
            return ESP_ERR_INVALID_ARG;
        h.targets[i] = (stage_target_t){t[i].ch, t[i].id, 0};
    }
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x41, "joint_cache");
    if (!p || p->size < 0x200000)
        return ESP_ERR_NOT_FOUND;
    if (esp_partition_read(p, 0, &old, sizeof(old)) == ESP_OK && old.magic == h.magic &&
        old.version == h.version && old.count == n && old.seal == seal(old) &&
        !memcmp(old.targets, h.targets, sizeof(h.targets)) && open_joint(p, &old) == ESP_OK)
        return ESP_OK;
    if (progress && !progress(0, n + 1))
        return ESP_ERR_INVALID_STATE;
    size_t bytes = 4096 + n * GODOX_DSP_WORDS * 4;
    esp_task_wdt_reset();
    esp_err_t e = esp_partition_erase_range(p, 0, (bytes + 4095) & ~4095u);
    esp_task_wdt_reset();
    h.hash = 2166136261u;
    for (unsigned i = 0; i < n && e == ESP_OK; i++) {
        if (progress && !progress(i, n + 1)) {
            e = ESP_ERR_INVALID_STATE;
            break;
        }
        uint32_t hz, gate;
        unsigned wifi;
        int32_t offset;
        uint8_t raw[10], bits[81];
        if (!godox_channel_plan(t[i].ch, &hz, &wifi, &offset) ||
            !godox_short_frame(t[i].id, GODOX_TEST, raw, bits) ||
            !godox_dsp_generate(bits, 81, offset, work, &gate)) {
            e = ESP_FAIL;
            break;
        }
        h.hash = stage_cache_hash(h.hash, work, GODOX_DSP_WORDS * 4);
        e = esp_partition_write(p, 4096 + i * GODOX_DSP_WORDS * 4, work, GODOX_DSP_WORDS * 4);
        esp_task_wdt_reset();
    }
    if (e == ESP_OK && progress && !progress(n, n + 1))
        e = ESP_ERR_INVALID_STATE;
    if (e == ESP_OK) {
        h.seal = seal(h);
        e = esp_partition_write(p, 0, &h, sizeof(h));
    }
    if (e == ESP_OK)
        e = open_joint(p, &h);
    if (e == ESP_OK && progress && !progress(n + 1, n + 1)) {
        joint_cache_close();
        e = ESP_ERR_INVALID_STATE;
    }
    return e;
}
