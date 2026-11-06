/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GODOX_FRAME_BYTES 10
#define GODOX_FRAME_BITS 81
typedef enum { GODOX_PREPARE = 0x11, GODOX_TEST = 0x09, GODOX_UNPREPARE = 0x21 } godox_command_t;
bool godox_encode_id(unsigned id, uint16_t *encoded);
bool godox_short_frame(unsigned id, godox_command_t command, uint8_t bytes[GODOX_FRAME_BYTES],
                       uint8_t bits[GODOX_FRAME_BITS]);
#define GODOX_PROPERTY_BYTES 12
#define GODOX_PROPERTY_BITS 97
/* group=0 ALL, 1..5 A..E. Manual power is tenths of a stop below full. */
bool godox_property_frame(unsigned id, unsigned group, uint8_t key, uint8_t value,
                          uint8_t bytes[GODOX_PROPERTY_BYTES], uint8_t bits[GODOX_PROPERTY_BITS]);
bool godox_channel_plan(unsigned ch, uint32_t *target_hz, unsigned *wifi_channel,
                        int32_t *offset_hz);
