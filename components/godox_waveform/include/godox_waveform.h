/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "godox_protocol.h"
#define GODOX_SAMPLE_RATE 40000000u
#define GODOX_SAMPLE_COUNT 16128u
#define GODOX_WINDOW_US 380u
const uint32_t *godox_waveform_get(godox_command_t command, size_t *count);
