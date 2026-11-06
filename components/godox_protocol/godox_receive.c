/* SPDX-License-Identifier: GPL-3.0-or-later; local GFSK discriminator + frame validator. */
#include "godox_receive.h"
#include "godox_protocol.h"
#include <math.h>
#include <string.h>
#ifdef __TINYC__
#define cosf(x) ((float)cos(x))
#define sinf(x) ((float)sin(x))
#define atan2f(y, x) ((float)atan2((y), (x)))
#endif
static float absolute(float x) { return x < 0 ? -x : x; }
static float phase(float y, float x);
typedef struct {
    const void *data;
    bool iq8;
    unsigned dec, samples_per_bit;
} iq_view_t;
static inline void components_at(const iq_view_t *view, unsigned k, int *i, int *q) {
    if (view->iq8) {
        const int8_t *p = view->data;
        *i = p[2 * k] * 4;
        *q = p[2 * k + 1] * 4;
    } else {
        uint32_t w = ((const uint32_t *)view->data)[k];
        *i = (int)((w & 1023) ^ 512) - 512;
        *q = (int)(((w >> 10) & 1023) ^ 512) - 512;
    }
}
/* Q15 mixers for quarter-MHz channel offsets at both native dump rates. */
static const int16_t oscillator16[64][2] = {
    {32767, 0},       {32609, 3212},    {32137, 6393},    {31356, 9512},    {30273, 12539},
    {28898, 15446},   {27245, 18204},   {25329, 20787},   {23170, 23170},   {20787, 25329},
    {18204, 27245},   {15446, 28898},   {12539, 30273},   {9512, 31356},    {6393, 32137},
    {3212, 32609},    {0, 32767},       {-3212, 32609},   {-6393, 32137},   {-9512, 31356},
    {-12539, 30273},  {-15446, 28898},  {-18204, 27245},  {-20787, 25329},  {-23170, 23170},
    {-25329, 20787},  {-27245, 18204},  {-28898, 15446},  {-30273, 12539},  {-31356, 9512},
    {-32137, 6393},   {-32609, 3212},   {-32767, 0},      {-32609, -3212},  {-32137, -6393},
    {-31356, -9512},  {-30273, -12539}, {-28898, -15446}, {-27245, -18204}, {-25329, -20787},
    {-23170, -23170}, {-20787, -25329}, {-18204, -27245}, {-15446, -28898}, {-12539, -30273},
    {-9512, -31356},  {-6393, -32137},  {-3212, -32609},  {0, -32767},      {3212, -32609},
    {6393, -32137},   {9512, -31356},   {12539, -30273},  {15446, -28898},  {18204, -27245},
    {20787, -25329},  {23170, -23170},  {25329, -20787},  {27245, -18204},  {28898, -15446},
    {30273, -12539},  {31356, -9512},   {32137, -6393},   {32609, -3212}};
static const int16_t oscillator40[160][2] = {
    {32767, 0},       {32742, 1286},    {32666, 2571},    {32540, 3851},    {32364, 5126},
    {32137, 6393},    {31862, 7649},    {31537, 8894},    {31163, 10126},   {30742, 11341},
    {30273, 12539},   {29757, 13718},   {29196, 14876},   {28589, 16011},   {27938, 17121},
    {27245, 18204},   {26509, 19260},   {25732, 20286},   {24916, 21280},   {24062, 22242},
    {23170, 23170},   {22242, 24062},   {21280, 24916},   {20286, 25732},   {19260, 26509},
    {18204, 27245},   {17121, 27938},   {16011, 28589},   {14876, 29196},   {13718, 29757},
    {12539, 30273},   {11341, 30742},   {10126, 31163},   {8894, 31537},    {7649, 31862},
    {6393, 32137},    {5126, 32364},    {3851, 32540},    {2571, 32666},    {1286, 32742},
    {0, 32767},       {-1286, 32742},   {-2571, 32666},   {-3851, 32540},   {-5126, 32364},
    {-6393, 32137},   {-7649, 31862},   {-8894, 31537},   {-10126, 31163},  {-11341, 30742},
    {-12539, 30273},  {-13718, 29757},  {-14876, 29196},  {-16011, 28589},  {-17121, 27938},
    {-18204, 27245},  {-19260, 26509},  {-20286, 25732},  {-21280, 24916},  {-22242, 24062},
    {-23170, 23170},  {-24062, 22242},  {-24916, 21280},  {-25732, 20286},  {-26509, 19260},
    {-27245, 18204},  {-27938, 17121},  {-28589, 16011},  {-29196, 14876},  {-29757, 13718},
    {-30273, 12539},  {-30742, 11341},  {-31163, 10126},  {-31537, 8894},   {-31862, 7649},
    {-32137, 6393},   {-32364, 5126},   {-32540, 3851},   {-32666, 2571},   {-32742, 1286},
    {-32767, 0},      {-32742, -1286},  {-32666, -2571},  {-32540, -3851},  {-32364, -5126},
    {-32137, -6393},  {-31862, -7649},  {-31537, -8894},  {-31163, -10126}, {-30742, -11341},
    {-30273, -12539}, {-29757, -13718}, {-29196, -14876}, {-28589, -16011}, {-27938, -17121},
    {-27245, -18204}, {-26509, -19260}, {-25732, -20286}, {-24916, -21280}, {-24062, -22242},
    {-23170, -23170}, {-22242, -24062}, {-21280, -24916}, {-20286, -25732}, {-19260, -26509},
    {-18204, -27245}, {-17121, -27938}, {-16011, -28589}, {-14876, -29196}, {-13718, -29757},
    {-12539, -30273}, {-11341, -30742}, {-10126, -31163}, {-8894, -31537},  {-7649, -31862},
    {-6393, -32137},  {-5126, -32364},  {-3851, -32540},  {-2571, -32666},  {-1286, -32742},
    {0, -32767},      {1286, -32742},   {2571, -32666},   {3851, -32540},   {5126, -32364},
    {6393, -32137},   {7649, -31862},   {8894, -31537},   {10126, -31163},  {11341, -30742},
    {12539, -30273},  {13718, -29757},  {14876, -29196},  {16011, -28589},  {17121, -27938},
    {18204, -27245},  {19260, -26509},  {20286, -25732},  {21280, -24916},  {22242, -24062},
    {23170, -23170},  {24062, -22242},  {24916, -21280},  {25732, -20286},  {26509, -19260},
    {27245, -18204},  {27938, -17121},  {28589, -16011},  {29196, -14876},  {29757, -13718},
    {30273, -12539},  {30742, -11341},  {31163, -10126},  {31537, -8894},   {31862, -7649},
    {32137, -6393},   {32364, -5126},   {32540, -3851},   {32666, -2571},   {32742, -1286}};
static void mix_grid(const iq_view_t *iq, unsigned n, int step, int dc_i, int dc_q, float *real,
                     float *imag) {
    unsigned index = 0;
    unsigned length = iq->dec * iq->samples_per_bit;
    const int16_t (*oscillator)[2] = length == 64 ? oscillator16 : oscillator40;
    unsigned stride = (unsigned)((step % (int)length + (int)length) % (int)length);
    for (unsigned k = 0; k < n; k++) {
        int32_t sum_i = 0, sum_q = 0;
        for (unsigned j = 0; j < iq->dec; j++) {
            int i, q;
            components_at(iq, k * iq->dec + j, &i, &q);
            i -= dc_i;
            q -= dc_q;
            int c = oscillator[index][0], s = oscillator[index][1];
            sum_i += i * c - q * s;
            sum_q += i * s + q * c;
            index += stride;
            if (index >= length)
                index -= length;
        }
        real[k] = sum_i;
        imag[k] = sum_q;
    }
}
static void discriminate(unsigned n, unsigned sps, const float *real, float *imag_energy,
                         float *f) {
    float previous_i = 0, previous_q = 0;
    float last_i = 0, last_q = 0, before_i = 0, before_q = 0;
    for (unsigned k = 0; k < n; k++) {
        float current_i = real[k], current_q = imag_energy[k];
        if (sps == 8) {
            /* Three-tap low pass before the2MHz phase discriminator. A single
             * half-us boxcar alone leaves noise that flips repeated-ID bits. */
            float raw_i = current_i, raw_q = current_q;
            current_i += 2 * last_i + before_i;
            current_q += 2 * last_q + before_q;
            before_i = last_i;
            before_q = last_q;
            last_i = raw_i;
            last_q = raw_q;
        }
        f[k] = k ? phase(current_q * previous_i - current_i * previous_q,
                         current_i * previous_i + current_q * previous_q) *
                       (39788.7358f * sps)
                 : 0;
        float now = current_i * current_i + current_q * current_q;
        float prior = previous_i * previous_i + previous_q * previous_q;
        imag_energy[k] = now < prior ? now : prior;
        previous_i = current_i;
        previous_q = current_q;
    }
}
/* Bounded-cost discriminator; phase error is small relative to the FSK margin.
 * Keep libm atan2 only for the final native-rate carrier check. */
static float phase(float y, float x) {
    float a = absolute(y) + 1e-20f, r, angle;
    if (x >= 0) {
        r = (x - a) / (x + a);
        angle = .7853981634f;
    } else {
        r = (x + a) / (a - x);
        angle = 2.3561944902f;
    }
    angle += (.1963f * r * r - .9817f) * r;
    return y < 0 ? -angle : angle;
}
static bool carrier_matches(const iq_view_t *iq, unsigned start, unsigned bits, int32_t target,
                            int dc_i, int dc_q, int32_t *measured) {
    int64_t dot = 0, cross = 0;
    for (unsigned k = start * iq->dec + 1; k < (start + 32 * iq->samples_per_bit) * iq->dec; k++) {
        int ai, aq, bi, bq;
        components_at(iq, k - 1, &ai, &aq);
        components_at(iq, k, &bi, &bq);
        ai -= dc_i;
        bi -= dc_i;
        aq -= dc_q;
        bq -= dc_q;
        dot += (int64_t)ai * bi + (int64_t)aq * bq;
        cross += (int64_t)ai * bq - (int64_t)aq * bi;
    }
    float frequency =
        atan2f((float)cross, (float)dot) * (39788.7358f * iq->samples_per_bit * iq->dec);
    if (measured)
        *measured = (int32_t)frequency;
    return absolute(frequency - target) < 180000;
}
static unsigned octet(const uint8_t *p) {
    unsigned v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 1) | p[i];
    return v;
}
static bool decode_header(const uint8_t *b, size_t n, unsigned *id) {
    if (!b || !id || n < 72)
        return false;
    for (size_t i = 0; i < n; i++)
        if (b[i] > 1)
            return false;
    for (int i = 0; i < 32; i++)
        if (b[i] != (uint8_t)(!(i & 1)))
            return false;
    unsigned encoded = (octet(b + 32) << 8) | octet(b + 40);
    if (encoded != ((octet(b + 48) << 8) | octet(b + 56)))
        return false;
    unsigned found = 100;
    for (unsigned i = 0; i < 100; i++)
        if (50024u / (i + 1) == encoded) {
            found = i;
            break;
        }
    if (found == 100)
        return false;
    unsigned kind = octet(b + 64);
    if (kind != 0xd5 && kind != 0xa9)
        return false;
    *id = found;
    return true;
}
bool godox_decode_bits(const uint8_t *b, size_t n, unsigned *id) {
    unsigned found;
    if (n < 81 || !id || !decode_header(b, n, &found))
        return false;
    unsigned kind = octet(b + 64);
    if (kind == 0xd5) {
        unsigned cmd = octet(b + 72);
        if (cmd != 0x11 && cmd != 0x09 && cmd != 0x21 && cmd != 0x19 && cmd != 0x1a && cmd != 0x0a)
            return false;
        if (b[80] != b[79])
            return false;
    } else if (kind == 0xa9) {
        unsigned target = octet(b + 72);
        if (n < 97 || (target != 0x50 && (target < 0x0a || target > 0x0e)) || b[96] != b[95])
            return false;
    } else
        return false;
    *id = found;
    return true;
}
static unsigned decode_capture(const iq_view_t *iq, size_t count, int32_t offset, unsigned *ids,
                               unsigned capacity, godox_rx_diagnostics_t *d, bool both_polarities,
                               int32_t *carriers, bool header_only) {
    if (d)
        memset(d, 0, sizeof(*d));
    unsigned required_bits = header_only ? 72 : 81;
    unsigned sps = iq->samples_per_bit;
    if (!iq->data || !ids || !capacity || count > 16380 || count < required_bits * sps * iq->dec)
        return 0;
    unsigned found = 0;
    float f[1024], energy[1024], raw_f[1024];
    /* At most16380 signed10-bit values, safely within int32. */
    int32_t sum_i = 0, sum_q = 0;
    for (size_t k = 0; k < count; k++) {
        int i, q;
        components_at(iq, k, &i, &q);
        sum_i += i;
        sum_q += q;
    }
    int dc_i = sum_i / (int32_t)count, dc_q = sum_q / (int32_t)count;
    unsigned n = (unsigned)(count / iq->dec);
    /* S3 dump words contain I,-Q. See pinned esp-sdr docs/protocol.md and
     * main/common/burst_capture.h, which transmits these fields unchanged. */
    for (int mirror = -1; mirror <= (both_polarities ? 1 : -1); mirror += 2) {
        float angle = -6.28318530718f * (float)(mirror * offset) / (250000.0f * sps * iq->dec);
        float cr = cosf(angle), ci = sinf(angle), rr = 1, ri = 0;
        if (offset % 250000 == 0) {
            mix_grid(iq, n, -mirror * (offset / 250000), dc_i, dc_q, raw_f, energy);
        } else {
            for (unsigned k = 0; k < n; k++) {
                float sr = 0, si = 0;
                for (unsigned j = 0; j < iq->dec; j++) {
                    int iv, qv;
                    components_at(iq, k * iq->dec + j, &iv, &qv);
                    iv -= dc_i;
                    qv -= dc_q;
                    sr += iv * rr - qv * ri;
                    si += iv * ri + qv * rr;
                    float tr = rr * cr - ri * ci;
                    ri = rr * ci + ri * cr;
                    rr = tr;
                }
                raw_f[k] = sr;
                energy[k] = si;
            }
        }
        discriminate(n, sps, raw_f, energy, f);
        memcpy(raw_f, f, n * sizeof(float));
        for (unsigned integration = 0; integration < 2; integration++) {
            float minimum = integration ? 60000 : 25000;
            if (integration) {
                for (unsigned k = 0; k + sps / 2 < n; k++)
                    f[k] = (raw_f[k + sps / 2 - 1] + raw_f[k + sps / 2]) * .5f;
            } else {
                /* Integrate the whole symbol, suppressing phase noise in quiet RF
                 * portions. In-place update only touches samples already consumed. */
                for (unsigned k = 0; k + sps <= n; k++) {
                    float weighted = 0, total = 0;
                    for (unsigned j = 0; j < sps; j++) {
                        weighted += raw_f[k + j] * energy[k + j];
                        total += energy[k + j];
                    }
                    f[k] = total > 0 ? weighted / total : 0;
                }
            }
            float sums[8] = {0};
            float alternating[8] = {0};
            for (unsigned p = 0; p < sps; p++)
                for (unsigned j = 0; j < 32; j++) {
                    float v = f[p + j * sps];
                    sums[p] += v;
                    alternating[p] += (j & 1) ? -v : v;
                }
            for (unsigned start = 0; start + required_bits * sps <= n; start++) {
                uint8_t b[97];
                unsigned length = required_bits;
                bool good = true;
                float mean = 0;
                unsigned timing = start & (sps - 1);
                float center = sums[timing] / 32;
                float correlation = alternating[timing] / 32;
                float leaving = f[start];
                float entering = f[start + 32 * sps];
                sums[timing] += entering - leaving;
                alternating[timing] = -alternating[timing] + leaving - entering;
                if (absolute(center) > 250000 || absolute(correlation) < minimum)
                    continue;
                /* Reject non-alternating preambles before decoding the payload.
                 * Sync diagnostics now survive a bad payload or unknown command. */
                unsigned matches = 0;
                for (unsigned j = 0; j < 32; j++) {
                    float v = f[start + j * sps] - center;
                    matches += ((v < 0) == !(j & 1));
                }
                unsigned score = matches > 16 ? matches : 32 - matches;
                if (d && score > d->best_sync_bits)
                    d->best_sync_bits = score;
                if (score != 32)
                    continue;
                if (d)
                    d->sync++;
                for (unsigned j = 0; j < length; j++) {
                    float v = f[start + j * sps] - center;
                    if (absolute(v) < minimum || absolute(v) > 260000) {
                        good = false;
                        break;
                    }
                    b[j] = v < 0;
                    if (j < 32)
                        mean += v;
                    if (j == 80 && (octet(b + 64) == 0xa9 || octet(b + 64) == (0xa9 ^ 255))) {
                        if (start + 97 * sps > n) {
                            good = false;
                            break;
                        }
                        length = 97;
                    }
                }
                if (!good || absolute(mean / 32) > 80000)
                    continue;
                if (d)
                    d->trials++;
                for (int polarity = 0; polarity < 2; polarity++) {
                    unsigned id;
                    if (header_only ? decode_header(b, length, &id)
                                    : godox_decode_bits(b, length, &id)) {
                        if (d)
                            d->frames++;
                        int32_t measured;
                        if (carrier_matches(iq, start, length, mirror * offset, dc_i, dc_q,
                                            &measured)) {
                            bool duplicate = false;
                            for (unsigned k = 0; k < found; k++)
                                if (ids[k] == id)
                                    duplicate = true;
                            if (!duplicate) {
                                if (carriers)
                                    carriers[found] = measured;
                                ids[found++] = id;
                            }
                            if (found == capacity)
                                return found;
                        } else if (d)
                            d->carrier_rejected++;
                    }
                    for (unsigned j = 0; j < length; j++)
                        b[j] ^= 1;
                }
            }
        }
    }
    return found;
}
bool godox_decode_capture_report(const uint32_t *iq, size_t count, int32_t offset, unsigned *id,
                                 godox_rx_diagnostics_t *d) {
    return decode_capture(&(iq_view_t){iq, false, 16, 4}, count, offset, id, 1, d, true, NULL,
                          false) != 0;
}
unsigned godox_decode_capture_ids(const uint32_t *iq, size_t count, int32_t offset, unsigned *ids,
                                  unsigned capacity, godox_rx_diagnostics_t *d) {
    /* Unknown-CH discovery must use the documented I,-Q capture convention.
     * Trying both spectral mirrors would invent a second CH at LO-offset. */
    return decode_capture(&(iq_view_t){iq, false, 16, 4}, count, offset, ids, capacity, d, false,
                          NULL, false);
}
unsigned godox_decode_iq8_ids(const int8_t *iq, size_t count, int32_t offset, unsigned *ids,
                              unsigned capacity, godox_rx_diagnostics_t *d) {
    return decode_capture(&(iq_view_t){iq, true, 16, 4}, count, offset, ids, capacity, d, false,
                          NULL, false);
}
bool godox_decode_capture(const uint32_t *iq, size_t count, int32_t offset, unsigned *id) {
    return godox_decode_capture_report(iq, count, offset, id, NULL);
}

unsigned godox_decode_iq8_carriers(const int8_t *iq, size_t count, int32_t offset, unsigned *ids,
                                   unsigned capacity, godox_rx_diagnostics_t *d,
                                   int32_t *carriers) {
    return decode_capture(&(iq_view_t){iq, true, 16, 4}, count, offset, ids, capacity, d, false,
                          carriers, false);
}

unsigned godox_decode_iq8_rate(const int8_t *iq, size_t count, unsigned rate, int32_t offset,
                               unsigned *ids, unsigned capacity, godox_rx_diagnostics_t *d,
                               int32_t *carriers) {
    if (rate != 16000000 && rate != 40000000) {
        if (d)
            memset(d, 0, sizeof(*d));
        return 0;
    }
    unsigned sps = rate == 40000000 ? 8 : 4;
    return decode_capture(&(iq_view_t){iq, true, rate / (250000 * sps), sps}, count, offset, ids,
                          capacity, d, false, carriers, false);
}

unsigned godox_verify_iq8_header(const int8_t *iq, size_t count, int32_t offset, unsigned *ids,
                                 unsigned capacity, godox_rx_diagnostics_t *d, int32_t *carriers) {
    /* Confirmation only. The scan requires a full16MS/s frame for this same
     * CH/ID before accepting this independent40MS/s header and carrier. */
    return decode_capture(&(iq_view_t){iq, true, 20, 8}, count, offset, ids, capacity, d, false,
                          carriers, true);
}
