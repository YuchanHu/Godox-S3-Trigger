/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool godox_decode_bits(const uint8_t *bits, size_t count, unsigned *id);
/* Native 16MS/s IQ10, tuned to the Wi-Fi LO from godox_channel_plan().
 * Includes both IQ polarities. Returns only protocol-validated full frames. */
bool godox_decode_capture(const uint32_t *iq, size_t count, int32_t offset_hz, unsigned *id);
typedef struct {
    uint32_t trials, sync, frames, carrier_rejected, best_sync_bits;
} godox_rx_diagnostics_t;
/* 40MS/s confirmation header: exact32-bit preamble, repeated16-bit ID and
 * known8-bit frame kind. Must be paired with a validated full16MS/s frame.
 * This API alone does not establish a discovered transmitter. */
unsigned godox_verify_iq8_header(const int8_t *iq, size_t count, int32_t offset, unsigned *ids,
                                 unsigned capacity, godox_rx_diagnostics_t *diagnostics,
                                 int32_t *carriers);
unsigned godox_decode_iq8_rate(const int8_t *iq, size_t count, unsigned sample_rate_hz,
                               int32_t offset, unsigned *ids, unsigned capacity,
                               godox_rx_diagnostics_t *diagnostics, int32_t *carriers);
unsigned godox_decode_iq8_carriers(const int8_t *iq, size_t count, int32_t offset, unsigned *ids,
                                   unsigned capacity, godox_rx_diagnostics_t *diagnostics,
                                   int32_t *native_carriers_hz);
/* Native S3 I,-Q, signed8 bits per component, same protocol/carrier validation. */
unsigned godox_decode_iq8_ids(const int8_t *iq, size_t count, int32_t offset_hz, unsigned *ids,
                              unsigned capacity, godox_rx_diagnostics_t *diagnostics);
/* Distinct IDs from native S3 I,-Q words, correcting frequency sign for CH discovery.
 * The legacy single-result diagnostic API above also tries conjugate IQ. */
unsigned godox_decode_capture_ids(const uint32_t *iq, size_t count, int32_t offset_hz,
                                  unsigned *ids, unsigned capacity,
                                  godox_rx_diagnostics_t *diagnostics);
bool godox_decode_capture_report(const uint32_t *iq, size_t count, int32_t offset_hz, unsigned *id,
                                 godox_rx_diagnostics_t *diagnostics);
