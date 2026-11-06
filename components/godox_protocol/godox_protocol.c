/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "godox_protocol.h"
bool godox_encode_id(unsigned id, uint16_t *encoded) {
    if (id > 99 || !encoded)
        return false;
    *encoded = 50024u / (id + 1);
    return true;
}
bool godox_property_frame(unsigned id, unsigned group, uint8_t key, uint8_t value,
                          uint8_t bytes[GODOX_PROPERTY_BYTES], uint8_t bits[GODOX_PROPERTY_BITS]) {
    uint8_t short_bytes[10], short_bits[81];
    if (!bytes || !bits || group > 5 || !godox_short_frame(id, GODOX_TEST, short_bytes, short_bits))
        return false;
    for (unsigned i = 0; i < 8; i++)
        bytes[i] = short_bytes[i];
    bytes[8] = 0xa9;
    bytes[9] = group ? 0x09 + group : 0x50;
    bytes[10] = key;
    bytes[11] = value;
    for (unsigned i = 0; i < 96; i++)
        bits[i] = (bytes[i / 8] >> (7 - i % 8)) & 1;
    bits[96] = bits[95];
    return true;
}
bool godox_channel_plan(unsigned ch, uint32_t *target_hz, unsigned *wifi_channel,
                        int32_t *offset_hz) {
    /* Rounded nominal frequencies from XProII FCC table, reproduced in
     * docs/stage_firmware.md. CH08 agrees with the successful local test. */
    static const uint16_t half_mhz[32] = {4826, 4829, 4832, 4836, 4839, 4842, 4846, 4849,
                                          4852, 4856, 4859, 4862, 4866, 4869, 4872, 4876,
                                          4879, 4882, 4886, 4889, 4892, 4896, 4899, 4902,
                                          4906, 4909, 4912, 4916, 4919, 4922, 4926, 4929};
    if (ch < 1 || ch > 32 || !target_hz || !wifi_channel || !offset_hz)
        return false;
    *target_hz = (uint32_t)half_mhz[ch - 1] * 500000u;
    *wifi_channel = 1 + (*target_hz - 2412000000u + 2499999u) / 5000000u; // ties select lower LO
    *offset_hz = (int32_t)((int64_t)*target_hz -
                           (2412000000LL + (*wifi_channel - 1) * 5000000LL));
    return true;
}
bool godox_short_frame(unsigned id, godox_command_t command, uint8_t bytes[GODOX_FRAME_BYTES],
                       uint8_t bits[GODOX_FRAME_BITS]) {
    uint16_t encoded;
    if (!bytes || !bits || !godox_encode_id(id, &encoded) ||
        (command != GODOX_PREPARE && command != GODOX_TEST && command != GODOX_UNPREPARE))
        return false;
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = 0xaa;
    bytes[4] = bytes[6] = encoded >> 8;
    bytes[5] = bytes[7] = encoded & 255;
    bytes[8] = 0xd5;
    bytes[9] = command;
    for (unsigned i = 0; i < 80; ++i)
        bits[i] = (bytes[i / 8] >> (7 - i % 8)) & 1;
    bits[80] = bits[79];
    return true;
}
