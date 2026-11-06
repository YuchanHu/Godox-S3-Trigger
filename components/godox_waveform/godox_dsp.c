/* SPDX-License-Identifier: GPL-3.0-or-later
 * Mathematical implementation of the documented Godox FSK PHY.
 * Offline-generated interpolation kernel in dsp_tables.h; no vendor code.
 */
#include "godox_dsp.h"
#include "dsp_tables.h"
#ifdef __wasm__
extern double cos(double), sin(double);
extern long lrint(double);
#else
#include <math.h>
#endif
static float fr[792], fi[792], sr[806], si[806];
bool godox_dsp_generate(const uint8_t *bits, size_t bit_count, int32_t offset_hz,
                        uint32_t words[GODOX_DSP_WORDS], uint32_t *window_us) {
    if (!bits || !words || !window_us || (bit_count != 81 && bit_count != 97) ||
        offset_hz % 500000 || offset_hz < -2500000 || offset_hz > 2500000)
        return false;
    for (size_t n = 0; n < bit_count; n++)
        if (bits[n] > 1)
            return false;
    const double tau = 6.2831853071795864769;
    size_t nf = bit_count * 8 + 16, ns = nf + 14;
    double phase = 0;
    for (size_t n = 0; n < nf; n++) {
        double deviation = 0;
        for (int j = 0; j < 17; j++) {
            int k = (int)n - j;
            if (k >= 0 && k % 8 == 0 && (size_t)(k / 8) < bit_count)
                deviation += (bits[k / 8] ? -1.0 : 1.0) * dsp_pulse[j];
        }
        phase += tau * deviation / 12.0; // 200kHz / 2MHz / 1.2
        fr[n] = (float)cos(phase);
        fi[n] = (float)sin(phase);
    }
    for (size_t n = 0; n < ns; n++) {
        double r = 0, i = 0;
        for (int j = 0; j < 15; j++) {
            int k = (int)n - j;
            if (k >= 0 && (size_t)k < nf) {
                r += fr[k] * dsp_fir[j];
                i += fi[k] * dsp_fir[j];
            }
        }
        sr[n] = (float)r;
        si[n] = (float)i;
    }
    double mr[80], mi[80];
    for (int n = 0; n < 80; n++) {
        double a = tau * offset_hz * n / 40000000.0;
        mr[n] = cos(a);
        mi[n] = sin(a);
    }
    const size_t trim = bit_count == 97 ? 160 : 0, guard = bit_count == 97 ? 80 : 320;
    const size_t length = ns * 20 - 2 * trim;
    if (guard + length > GODOX_DSP_WORDS)
        return false;
    for (size_t n = 0; n < GODOX_DSP_WORDS; n++)
        words[n] = 0;
    for (size_t n = 0; n < length; n++) {
        size_t source = n + trim;
        int centre = source / 20;
        double r = 0, i = 0;
        for (int k = centre - 12; k <= centre + 12; k++) {
            int tap = (int)source - k * 20 + 240;
            if (k >= 0 && (size_t)k < ns && tap >= 0 && tap <= 480) {
                r += sr[k] * dsp_sinc[tap];
                i += si[k] * dsp_sinc[tap];
            }
        }
        size_t m = n % 80;
        long ri = lrint(384 * (r * mr[m] - i * mi[m])), rq = lrint(384 * (r * mi[m] + i * mr[m]));
        if (ri < -512 || ri > 511 || rq < -512 || rq > 511)
            return false;
        words[n + guard] = ((uint32_t)ri & 1023u) | (((uint32_t)rq & 1023u) << 10);
    }
    /* Property frames: trim only outer filter transients, keeping all 97
     * symbol centres. Active range 2..397us; software gate 398us <403.2us.
     * This new envelope and RF timing require board validation. */
    *window_us = bit_count == 97 ? 398 : 380;
    return true;
}
