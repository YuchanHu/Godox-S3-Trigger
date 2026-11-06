/* SPDX-License-Identifier: GPL-3.0-or-later
 * Local FFT detector and band planner. Candidate-first architecture and the
 * second frequency table follow Ran-ying/esp32-godox-flash c05b3a9,
 * main/detect_w32.c and main/dsp/channels.inc (GPL-3.0-or-later).
 */
#include "godox_scan.h"
#include "godox_protocol.h"
#include <math.h>
#include <string.h>
#ifdef __TINYC__
#define cosf(x) ((float)cos(x))
#define sinf(x) ((float)sin(x))
#endif
enum { N = 256 };
static float twiddle[N / 2][2], window[N];
static const unsigned wifi_bands[6] = {2, 4, 6, 8, 10, 11};
unsigned godox_scan_wifi(unsigned band) { return band < 6 ? wifi_bands[band] : 0; }
unsigned godox_scan_band(unsigned ch) { return ch >= 1 && ch <= 32 ? (ch - 1) / 6 : 6; }
int32_t godox_scan_offset(unsigned ch, unsigned variant, unsigned wifi) {
    uint32_t hz;
    unsigned unused;
    int32_t offset;
    if (variant > 1 || wifi < 1 || wifi > 13 || !godox_channel_plan(ch, &hz, &unused, &offset))
        return INT32_MAX;
    static const int16_t delta_khz[32] = {0,   0,   0, 0,   0,   0, 0,   0,   750, 0,   0,
                                          750, 0,   0, 250, 0,   0, 750, 0,   0,   750, 0,
                                          0,   750, 0, 0,   750, 0, 0,   750, 0,   0};
    return (int32_t)((int64_t)hz - (2412000000LL + (wifi - 1) * 5000000LL)) +
           (variant ? delta_khz[ch - 1] * 1000 : 0);
}
void godox_scan_init(void) {
    for (unsigned k = 0; k < N / 2; k++) {
        twiddle[k][0] = cosf(-6.28318530718f * k / N);
        twiddle[k][1] = sinf(-6.28318530718f * k / N);
    }
    for (unsigned k = 0; k < N; k++)
        window[k] = .5f - .5f * cosf(6.28318530718f * k / (N - 1));
}
static void fft(float re[N], float im[N]) {
    for (unsigned i = 1, j = 0; i < N; i++) {
        unsigned bit = N / 2;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float v = re[i];
            re[i] = re[j];
            re[j] = v;
            v = im[i];
            im[i] = im[j];
            im[j] = v;
        }
    }
    for (unsigned length = 2; length <= N; length *= 2) {
        unsigned half = length / 2, step = N / length;
        for (unsigned base = 0; base < N; base += length)
            for (unsigned j = 0; j < half; j++) {
                float c = twiddle[j * step][0], s = twiddle[j * step][1];
                unsigned a = base + j, b = a + half;
                float r = re[b] * c - im[b] * s, i = re[b] * s + im[b] * c;
                re[b] = re[a] - r;
                im[b] = im[a] - i;
                re[a] += r;
                im[a] += i;
            }
    }
}
unsigned godox_scan_detect_wifi(const int8_t *iq, size_t count, unsigned wifi,
                                godox_scan_candidate_t out[GODOX_SCAN_CANDIDATES]) {
    if (!iq || !out || count < 5184 || count > 16380 || wifi < 1 || wifi > 13)
        return 0;
    float re[N], im[N], power[N] = {0};
    for (unsigned w = 0; w < 9; w++) {
        unsigned at = (count - N) * w / 8;
        float mi = 0, mq = 0;
        for (unsigned k = 0; k < N; k++) {
            mi += iq[2 * (at + k)];
            mq += iq[2 * (at + k) + 1];
        }
        mi /= N;
        mq /= N;
        for (unsigned k = 0; k < N; k++) {
            re[k] = (iq[2 * (at + k)] - mi) * window[k];
            im[k] = (iq[2 * (at + k) + 1] - mq) * window[k];
        }
        fft(re, im);
        for (unsigned k = 0; k < N; k++) {
            float p = re[k] * re[k] + im[k] * im[k];
            if (p > power[k])
                power[k] = p;
        }
    }
    unsigned n = 0;
    for (unsigned ch = 1; ch <= 32; ch++) {

        int32_t previous = INT32_MAX;
        for (unsigned v = 0; v < 2; v++) {
            int32_t offset = godox_scan_offset(ch, v, wifi);
            if (offset > 5000000 || offset < -5000000 || offset == previous)
                continue;
            previous = offset;
            int bin = (int)lroundf(-(float)offset * N / 16000000);
            float peak = 0, noise = 1;
            for (int d = -4; d <= 4; d++) {
                float p = power[(bin + d) & (N - 1)];
                if (p > peak)
                    peak = p;
            }
            for (int d = 8; d <= 12; d++)
                noise += power[(bin + d) & (N - 1)] + power[(bin - d) & (N - 1)];
            godox_scan_candidate_t c = {ch, offset, peak / (noise / 10)};
            if (n == GODOX_SCAN_CANDIDATES)
                continue;
            unsigned pos = n++;
            while (pos && out[pos - 1].score < c.score) {
                out[pos] = out[pos - 1];
                pos--;
            }
            out[pos] = c;
        }
    }
    return n;
}

unsigned godox_scan_detect(const int8_t *iq, size_t count, unsigned band,
                           godox_scan_candidate_t out[GODOX_SCAN_CANDIDATES]) {
    return godox_scan_detect_wifi(iq, count, godox_scan_wifi(band), out);
}
unsigned godox_scan_locate(unsigned wifi, int32_t native_hz) {
    if (wifi < 1 || wifi > 13 || native_hz < -20000000 || native_hz > 20000000)
        return 0;
    int best = 16000000, second = 16000000;
    unsigned found = 0;
    for (unsigned ch = 1; ch <= 32; ch++) {
        int distance = 16000000;
        for (unsigned v = 0; v < 2; v++) {
            int d = godox_scan_offset(ch, v, wifi) + native_hz;
            if (d < 0)
                d = -d;
            if (d < distance)
                distance = d;
        }
        if (distance < best) {
            second = best;
            best = distance;
            found = ch;
        } else if (distance < second)
            second = distance;
    }
    return best < 100000 && second - best >= 50000 ? found : 0;
}
int godox_scan_confirm(godox_scan_confirmation_t *s, unsigned ch, unsigned id, unsigned rate,
                       uint32_t ms) {
    if (!s || ch < 1 || ch > 32 || id > 99 || (rate != 16000000 && rate != 40000000))
        return 0;
    unsigned mask = rate == 16000000 ? 1 : 2;
    unsigned oldest = 0;
    uint32_t age = 0;
    for (unsigned k = 0; k < 32; k++) {
        godox_scan_vote_t *v = &s->votes[k];
        if (v->ch == ch && v->id == id && (uint32_t)(ms - v->last_ms) < 30000) {
            v->last_ms = ms;
            v->rate_mask |= mask;
            if (v->rate_mask & (v->rate_mask - 1))
                return 1;
            return 0;
        }
        if (!v->ch && age != UINT32_MAX) {
            oldest = k;
            age = UINT32_MAX;
        } else if (age != UINT32_MAX && (uint32_t)(ms - v->last_ms) > age) {
            oldest = k;
            age = ms - v->last_ms;
        }
    }
    /* A40MS/s header cannot create evidence on its own. */
    if (rate != 16000000)
        return 0;
    s->votes[oldest] = (godox_scan_vote_t){ms, ms - 2000u, mask, ch, id};
    return -1;
}

unsigned godox_scan_pending(godox_scan_confirmation_t *s, uint32_t ms) {
    unsigned chosen = 32;
    uint32_t oldest = 0;
    for (unsigned k = 0; k < 32; k++) {
        godox_scan_vote_t *v = &s->votes[k];
        uint32_t waiting = ms - v->request_ms;
        if (v->ch && v->rate_mask == 1 && (uint32_t)(ms - v->last_ms) < 30000 && waiting >= 1500 &&
            (chosen == 32 || waiting > oldest)) {
            chosen = k;
            oldest = waiting;
        }
    }
    if (chosen == 32)
        return 0;
    s->votes[chosen].request_ms = ms;
    return s->votes[chosen].ch;
}
