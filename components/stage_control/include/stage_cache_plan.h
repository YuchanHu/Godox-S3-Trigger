/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "godox_dsp.h"
#include "stage_core.h"
#include <stddef.h>
#define STAGE_CACHE_MAGIC 0x57415643u
#define STAGE_CACHE_VERSION 1u
#define STAGE_CACHE_HEADER_BYTES 4096u
#define STAGE_CACHE_MAX_ENTRIES 9u
#define STAGE_CACHE_BYTES 0xA0000u
typedef struct {
    uint32_t offset, window_us;
    uint8_t type, group, key, value;
} stage_cache_entry_t;
typedef struct {
    uint32_t magic, version;
    uint8_t ch, group, id, power;
    uint32_t count, data_hash, header_hash;
    stage_cache_entry_t entries[STAGE_CACHE_MAX_ENTRIES];
} stage_cache_header_t;
bool stage_cache_same_rf(const stage_config_t *a, const stage_config_t *b);
bool stage_cache_plan(const stage_config_t *c, stage_cache_header_t *header);
bool stage_cache_header_valid(const stage_cache_header_t *h, const stage_config_t *c);
uint32_t stage_cache_hash(uint32_t seed, const void *data, size_t size);
void stage_cache_seal(stage_cache_header_t *h, uint32_t data_hash);
bool stage_cache_compile(const stage_config_t *c, const stage_cache_header_t *h, unsigned index,
                         uint32_t *words);
