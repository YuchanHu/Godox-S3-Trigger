/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define GODOX_SCAN_BANDS 6u
#define GODOX_SCAN_CANDIDATES 12u
typedef struct {
    unsigned ch;
    int32_t offset;
    float score;
} godox_scan_candidate_t;
/* Six overlapping receive bands cover both known frequency-table variants. */
unsigned godox_scan_wifi(unsigned band);
int32_t godox_scan_offset(unsigned ch, unsigned variant, unsigned wifi);
unsigned godox_scan_band(unsigned ch);
void godox_scan_init(void);
/* Nine short FFTs cover a complete324us frame anywhere in the1ms window.
 * Returns candidates in descending score, including both frequency variants. */
unsigned godox_scan_detect(const int8_t *iq, size_t count, unsigned band,
                           godox_scan_candidate_t out[GODOX_SCAN_CANDIDATES]);
unsigned godox_scan_detect_wifi(const int8_t *iq, size_t count, unsigned wifi,
                                godox_scan_candidate_t out[GODOX_SCAN_CANDIDATES]);
/* Resolve measured native I/-Q frequency against both physical-frequency tables.
 * Zero means the closest channel is too distant or ambiguous. */
unsigned godox_scan_locate(unsigned wifi, int32_t native_hz);
typedef struct {
    uint32_t last_ms, request_ms;
    uint16_t rate_mask;
    uint8_t ch, id;
} godox_scan_vote_t;
typedef struct {
    godox_scan_vote_t votes[32];
} godox_scan_confirmation_t;
/* -1 new full-frame candidate,0 still pending,1 confirmed at16 and40MS/s.
 * 40MS/s headers can only confirm an existing16MS/s full-frame candidate. */
int godox_scan_confirm(godox_scan_confirmation_t *s, unsigned ch, unsigned id, unsigned rate,
                       uint32_t ms);
/* Fair retry selection independent of receiving another full frame. Returns CH
 * or0; caller spaces requests >=1500ms. Evidence expires after30s. */
unsigned godox_scan_pending(godox_scan_confirmation_t *s, uint32_t ms);
