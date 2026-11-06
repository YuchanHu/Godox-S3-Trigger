/* SPDX-License-Identifier: GPL-3.0-or-later; pure C cache metadata and waveform plan. */
#include "stage_cache_plan.h"
#include "godox_protocol.h"
#include <string.h>
bool stage_cache_same_rf(const stage_config_t *a, const stage_config_t *b) {
    return a && b && a->ch == b->ch && a->group == b->group && a->id == b->id &&
           a->power == b->power;
}
uint32_t stage_cache_hash(uint32_t seed, const void *data, size_t size) {
    const uint8_t *p = data;
    for (size_t i = 0; i < size; i++)
        seed = (seed ^ p[i]) * 16777619u;
    return seed;
}
static void entry(stage_cache_header_t *h, uint8_t type, uint8_t group, uint8_t key,
                  uint8_t value) {
    unsigned i = h->count++;
    h->entries[i] = (stage_cache_entry_t){STAGE_CACHE_HEADER_BYTES + i * GODOX_DSP_WORDS * 4,
                                          type == 0xa9 ? 398 : 380,
                                          type,
                                          group,
                                          key,
                                          value};
}
bool stage_cache_plan(const stage_config_t *c, stage_cache_header_t *h) {
    if (!h || !stage_config_valid(c))
        return false;
    memset(h, 0, sizeof(*h));
    h->magic = STAGE_CACHE_MAGIC;
    h->version = STAGE_CACHE_VERSION;
    h->ch = c->ch;
    h->group = c->group;
    h->id = c->id;
    h->power = c->power;
    entry(h, GODOX_PREPARE, 0, 0, 0);
    entry(h, 0xa9, c->group, 0xb1, 1);
    unsigned first = c->group ? c->group : 1, last = c->group ? c->group : 5;
    for (unsigned g = first; g <= last; g++)
        entry(h, 0xa9, g, 0xbc, (7 - c->power) * 10);
    entry(h, GODOX_TEST, 0, 0, 0);
    entry(h, GODOX_UNPREPARE, 0, 0, 0);
    return true;
}
void stage_cache_seal(stage_cache_header_t *h, uint32_t data_hash) {
    h->data_hash = data_hash;
    h->header_hash = 0;
    h->header_hash = stage_cache_hash(2166136261u, h, sizeof(*h));
}
bool stage_cache_header_valid(const stage_cache_header_t *h, const stage_config_t *c) {
    stage_cache_header_t expected, copy;
    if (!h || !stage_cache_plan(c, &expected) || h->magic != STAGE_CACHE_MAGIC ||
        h->version != STAGE_CACHE_VERSION || h->ch != c->ch || h->group != c->group ||
        h->id != c->id || h->power != c->power || h->count != expected.count ||
        memcmp(h->entries, expected.entries, sizeof(h->entries)))
        return false;
    copy = *h;
    copy.header_hash = 0;
    return h->header_hash == stage_cache_hash(2166136261u, &copy, sizeof(copy));
}
bool stage_cache_compile(const stage_config_t *c, const stage_cache_header_t *h, unsigned index,
                         uint32_t *words) {
    if (!c || !h || !words || index >= h->count || h->count > STAGE_CACHE_MAX_ENTRIES)
        return false;
    uint32_t hz, gate;
    unsigned wifi;
    int32_t offset;
    uint8_t raw[12], bits[97];
    if (!godox_channel_plan(c->ch, &hz, &wifi, &offset))
        return false;
    const stage_cache_entry_t *e = &h->entries[index];
    bool good = e->type == 0xa9 ? godox_property_frame(c->id, e->group, e->key, e->value, raw, bits)
                                : godox_short_frame(c->id, (godox_command_t)e->type, raw, bits);
    return good && godox_dsp_generate(bits, e->type == 0xa9 ? 97 : 81, offset, words, &gate) &&
           gate == e->window_us;
}
