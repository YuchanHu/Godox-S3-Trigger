/* SPDX-License-Identifier: GPL-3.0-or-later
 * SSD1681 sequencer. LUT/register baseline from Waveshare EPD_1in54_V2.c (MIT).
 * RAM addressing and explicit reference synchronization are local.
 */
#include "stage_epd.h"
#include "epd_lut.h"

typedef struct {
    const stage_epd_io_t *io;
    esp_err_t error;
} panel_t;
static const stage_rect_t screen = {0, 0, 200, 200};

static void transfer(panel_t *p, bool data, const uint8_t *b, size_t n) {
    if (p->error == ESP_OK)
        p->error = p->io->transfer(p->io->ctx, data, b, n);
}
static void command(panel_t *p, uint8_t c) { transfer(p, false, &c, 1); }
static void bytes(panel_t *p, const uint8_t *b, size_t n) { transfer(p, true, b, n); }
static void reg(panel_t *p, uint8_t c, uint8_t v) {
    command(p, c);
    bytes(p, &v, 1);
}
static void wait_idle(panel_t *p) {
    if (p->error == ESP_OK)
        p->error = p->io->wait_idle(p->io->ctx);
}
static void reset(panel_t *p) {
    if (p->error == ESP_OK)
        p->error = p->io->reset(p->io->ctx);
}
static void activate(panel_t *p, uint8_t flags) {
    reg(p, 0x22, flags);
    command(p, 0x20);
    wait_idle(p);
}
static void window(panel_t *p, const stage_rect_t *r) {
    int ys = 199 - r->y, ye = 200 - r->y - r->h;
    const uint8_t x[] = {(uint8_t)(r->x / 8), (uint8_t)((r->x + r->w) / 8 - 1)};
    const uint8_t y[] = {(uint8_t)ys, (uint8_t)(ys >> 8), (uint8_t)ye, (uint8_t)(ye >> 8)};
    /* Reset restores 0x11 to 0x03 (Y increment). Our rows require Y decrement.
     * Set it with every window, including immediately after partial-mode reset. */
    reg(p, 0x11, 0x01);
    command(p, 0x44);
    bytes(p, x, sizeof(x));
    command(p, 0x45);
    bytes(p, y, sizeof(y));
    reg(p, 0x4e, x[0]);
    command(p, 0x4f);
    bytes(p, y, 2);
}
static void lut(panel_t *p, const uint8_t *b) {
    command(p, 0x32);
    bytes(p, b, 153);
    wait_idle(p);
    reg(p, 0x3f, b[153]);
    reg(p, 0x03, b[154]);
    command(p, 0x04);
    bytes(p, b + 155, 3);
    reg(p, 0x2c, b[158]);
}
static void geometry(panel_t *p) {
    const uint8_t gate[] = {0xc7, 0, 1};
    command(p, 0x01);
    bytes(p, gate, sizeof(gate));
    window(p, &screen);
}
static void init_full(panel_t *p) {
    reset(p);
    command(p, 0x12);
    wait_idle(p);
    geometry(p);
    reg(p, 0x3c, 1);
    reg(p, 0x18, 0x80);
    activate(p, 0xb1);
    lut(p, epd_full_lut);
}
static void init_partial(panel_t *p) {
    reset(p);
    geometry(p);
    reg(p, 0x18, 0x80);
    lut(p, epd_partial_lut);
    /* Own the old/new banks explicitly; do not combine host synchronization
     * with the automatic RAM ping-pong bit from the vendor full-frame example. */
    const uint8_t options[10] = {0};
    command(p, 0x37);
    bytes(p, options, sizeof(options));
    reg(p, 0x3c, 0x80);
    activate(p, 0xc0);
}
static void write_bank(panel_t *p, uint8_t bank, const stage_rect_t *r, const uint8_t *frame,
                       uint8_t *scratch) {
    if (p->error != ESP_OK)
        return;
    size_t n = stage_frame_pack(frame, r, scratch);
    if (!n) {
        p->error = ESP_ERR_INVALID_ARG;
        return;
    }
    window(p, r);
    command(p, bank);
    bytes(p, scratch, n);
}
esp_err_t stage_epd_refresh(const stage_epd_io_t *io, bool full, const uint8_t *old_frame,
                            const uint8_t *frame, uint8_t *scratch, const stage_rect_t *dirty) {
    if (!io || !io->transfer || !io->reset || !io->wait_idle || !old_frame || !frame || !scratch ||
        !dirty || !stage_frame_pack(frame, dirty, scratch))
        return ESP_ERR_INVALID_ARG;
    panel_t p = {io, ESP_OK};
    if (full) {
        init_full(&p);
        write_bank(&p, 0x24, &screen, frame, scratch);
        write_bank(&p, 0x26, &screen, frame, scratch);
        window(&p, &screen);
        activate(&p, 0xc7);
        if (p.error == ESP_OK) {
            init_partial(&p);
            /* Hardware reset may invalidate controller state/RAM. Seed both
             * complete banks AFTER that reset, before the first dirty update. */
            write_bank(&p, 0x24, &screen, frame, scratch);
            write_bank(&p, 0x26, &screen, frame, scratch);
        }
    } else {
        write_bank(&p, 0x26, dirty, old_frame, scratch);
        write_bank(&p, 0x24, dirty, frame, scratch);
        /* 0x44/0x45 are RAM write windows, not a display clipping guarantee. */
        window(&p, &screen);
        activate(&p, 0xcf);
        /* Only after BUSY releases may the reference advance to the shown
         * image. This also fixes stale areas outside the next dirty rectangle. */
        write_bank(&p, 0x26, dirty, frame, scratch);
    }
    window(&p, &screen);
    return p.error;
}
