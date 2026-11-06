/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stage_view.h"
#include "stage_layout.h"
#include "ui_font.h"
#include <stdio.h>
#include <string.h>
static uint8_t *fb;
static void pixel(int x, int y, bool black) {
    if (x < 0 || x >= 200 || y < 0 || y >= 200)
        return;
    uint8_t bit = 0x80 >> (x & 7);
    if (black)
        fb[y * 25 + x / 8] &= ~bit;
    else
        fb[y * 25 + x / 8] |= bit;
}
static void box(int x, int y, int w, int h, bool fill, bool black) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (fill || i == 0 || j == 0 || i == w - 1 || j == h - 1)
                pixel(x + i, y + j, black);
}
static uint32_t next_cp(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp = *s++;
    int n = 0;
    if (cp >= 0xf0) {
        cp &= 7;
        n = 3;
    } else if (cp >= 0xe0) {
        cp &= 15;
        n = 2;
    } else if (cp >= 0xc0) {
        cp &= 31;
        n = 1;
    }
    for (int i = 0; i < n && *s; i++)
        cp = (cp << 6) | (*s++ & 63);
    *p = (const char *)s;
    return cp;
}
static const ui_font_t *font(int family, int size) {
    for (unsigned i = 0; i < sizeof(ui_fonts) / sizeof(ui_fonts[0]); i++)
        if (ui_fonts[i].family == family && ui_fonts[i].size == size)
            return &ui_fonts[i];
    return &ui_fonts[0];
}
static const ui_glyph_t *glyph(const ui_font_t *f, uint32_t cp) {
    for (unsigned i = 0; i < f->count; i++)
        if (f->glyphs[i].cp == cp)
            return &f->glyphs[i];
    return NULL;
}
static int width(const char *s, const ui_font_t *f) {
    int w = 0;
    while (*s) {
        const ui_glyph_t *g = glyph(f, next_cp(&s));
        w += g ? g->advance : f->size;
    }
    return w;
}
static stage_area_t ink_bounds(const char *s, const ui_font_t *f) {
    int minx = 10000, miny = 10000, maxx = 0, maxy = 0, x = 0;
    while (*s) {
        const ui_glyph_t *g = glyph(f, next_cp(&s));
        if (!g) {
            x += f->size;
            continue;
        }
        if (g->iw && g->ih) {
            if (x + g->ix < minx)
                minx = x + g->ix;
            if (g->iy < miny)
                miny = g->iy;
            if (x + g->ix + g->iw > maxx)
                maxx = x + g->ix + g->iw;
            if (g->iy + g->ih > maxy)
                maxy = g->iy + g->ih;
        }
        x += g->advance;
    }
    return minx == 10000 ? (stage_area_t){0} : (stage_area_t){minx, miny, maxx - minx, maxy - miny};
}
static void paint(int x, int y, const char *s, const ui_font_t *f, bool white) {
    while (*s) {
        const ui_glyph_t *g = glyph(f, next_cp(&s));
        if (!g) {
            x += f->size;
            continue;
        }
        for (int yy = 0; yy < g->h; yy++)
            for (int xx = 0; xx < g->w; xx++) {
                unsigned bit = yy * g->w + xx;
                if (f->bits[g->offset + bit / 8] & (0x80 >> (bit & 7)))
                    pixel(x + xx, y + yy, !white);
            }
        x += g->advance;
    }
}
static void desc(int x, int y, const char *s, bool white) {
    paint(x, y, s, font(UI_FONT_QIU, 16), white);
}
static void centre_desc(int x, int y, const char *s, bool white) {
    const ui_font_t *f = font(UI_FONT_QIU, 16);
    paint(x - width(s, f) / 2, y, s, f, white);
}
/* align: -1 left, 0 centre, 1 right. Trimming removes blank margins only;
 * every native bitmap pixel maps to exactly one panel pixel. */
static void number(int x, int y, const char *s, int size, int align, bool trim, bool white) {
    const ui_font_t *f = font(UI_FONT_SPLEEN, size);
    stage_area_t r = trim ? ink_bounds(s, f) : (stage_area_t){0, 0, width(s, f), size};
    x -= r.x + (align == 0 ? r.w / 2 : (align > 0 ? r.w : 0));
    paint(x, y - r.y, s, f, white);
}
enum { BUTTON_VALUE = 1, BUTTON_SELECTED = 2, BUTTON_DISABLED = 4 };
static void button(stage_area_t r, const char *label, unsigned flags) {
    bool selected = flags & BUTTON_SELECTED;
    box(r.x, r.y, r.w, r.h, selected, true);
    if (flags & BUTTON_VALUE) {
        int size = strchr(label, '/') ? 24 : 32;
        stage_area_t ink = ink_bounds(label, font(UI_FONT_SPLEEN, size));
        number(r.x + r.w / 2, r.y + (r.h - ink.h) / 2, label, size, 0, true, selected);
    } else
        centre_desc(r.x + r.w / 2, r.y + (r.h - 20) / 2, label, selected);
    if (flags & BUTTON_DISABLED)
        for (int y = r.y; y < r.y + r.h; y++)
            for (int x = r.x; x < r.x + r.w; x++)
                if ((x + y) % 2)
                    pixel(x, y, false);
}
static void battery(const stage_ui_t *s) {
    char text[12];
    if (s->battery_valid)
        snprintf(text, sizeof(text), "%d%%", s->battery_percent);
    else
        snprintf(text, sizeof(text), "--");
    stage_area_t ink = ink_bounds(text, font(UI_FONT_SPLEEN, 16));
    int text_x = 193 - ink.w;
    int icon_x = text_x - 4 - 20; // Exactly four blank pixels between cap and number ink.
    int icon_y = (STAGE_HEADER_H - 10) / 2;
    box(icon_x, icon_y, 18, 10, false, false);
    box(icon_x + 18, icon_y + 3, 2, 4, true, false);
    if (s->battery_valid) {
        int bars = s->battery_percent / 25;
        if (bars > 4)
            bars = 4;
        if (!bars && s->battery_percent > 0)
            bars = 1;
        for (int i = 0; i < bars; i++)
            box(icon_x + 2 + i * 4, icon_y + 2, 3, 6, true, false);
    }
    number(193, (STAGE_HEADER_H - ink.h) / 2, text, 16, 1, true, true);
}
static void title(const stage_ui_t *s, const char *label) {
    box(0, 0, 200, STAGE_HEADER_H, true, true);
    desc(8, 2, label, true);
    battery(s);
}
static const char *names[6] = {"频道", "分组", "无线ID", "功率", "次数", "间隔 ms"};
static void value(char *out, size_t size, int field, uint32_t v) {
    if (field == 1) {
        if (v)
            snprintf(out, size, "%c", 'A' + (int)v - 1);
        else
            snprintf(out, size, "ALL");
    } else if (field == 2 && !v)
        snprintf(out, size, "OFF");
    else if (field == 3)
        snprintf(out, size, "1/%u", 128u >> v);
    else if (field == 0 || field == 2)
        snprintf(out, size, "%02lu", (unsigned long)v);
    else
        snprintf(out, size, "%lu", (unsigned long)v);
}
static void card(stage_area_t r, const char *label, const char *data) {
    box(r.x, r.y, r.w, r.h, false, true);
    box(r.x + 6, r.y - 10, width(label, font(UI_FONT_QIU, 16)) + 8, 20, true, false);
    desc(r.x + 10, r.y - 10, label, false);
    number(r.x + r.w / 2, r.y + 14, data, strchr(data, '/') ? 24 : 32, 0, true, false);
}
static void footer(const char *hint) {
    box(0, STAGE_FOOTER_Y, 200, STAGE_FOOTER_H, true, true);
    centre_desc(100, STAGE_FOOTER_Y, hint, true);
}
static void preparation_detail(const stage_ui_t *s, char *out, size_t size) {
    if (s->prep_phase == STAGE_PREP_START) {
        snprintf(out, size, "准备重建波形");
        return;
    }
    if (s->prep_phase == STAGE_PREP_ERASE) {
        snprintf(out, size, "正在擦除旧缓存");
        return;
    }
    if (s->prep_phase == STAGE_PREP_HEADER) {
        snprintf(out, size, "正在保存缓存索引");
        return;
    }
    if (s->prep_wave == STAGE_PREP_WAVE_NONE) {
        snprintf(out, size, "正在检查已有缓存");
        return;
    }
    char target[24];
    switch (s->prep_wave) {
    case STAGE_PREP_WAVE_PREPARE:
        snprintf(target, sizeof(target), "准备");
        break;
    case STAGE_PREP_WAVE_MODE:
        snprintf(target, sizeof(target), "模式");
        break;
    case STAGE_PREP_WAVE_POWER:
        snprintf(target, sizeof(target), "功率%c",
                 s->prep_group >= 1 && s->prep_group <= 5 ? 'A' + s->prep_group - 1 : '?');
        break;
    case STAGE_PREP_WAVE_TEST:
        snprintf(target, sizeof(target), "引闪");
        break;
    case STAGE_PREP_WAVE_END:
        snprintf(target, sizeof(target), "收尾");
        break;
    default:
        snprintf(target, sizeof(target), "控制");
        break;
    }
    const char *action = s->prep_phase == STAGE_PREP_COMPUTE ? "计算"
                         : s->prep_phase == STAGE_PREP_WRITE ? "保存"
                                                             : "校验";
    snprintf(out, size, "%s%s波形", action, target);
}
void stage_view_render(const stage_ui_t *s, uint8_t frame[STAGE_FRAME_BYTES]) {
    fb = frame;
    memset(frame, 0xff, STAGE_FRAME_BYTES);
    char tmp[64];
    if (s->sleep_requested)
        return;
    if (s->preparing) {
        title(s, s->joint_active ? "共同引闪" : "引闪控制");
        box(8, 43, 184, 113, false, true);
        centre_desc(100, 52, "正在准备", false);
        snprintf(tmp, sizeof(tmp), "%u / %u", s->prep_done, s->prep_total);
        number(100, 76, tmp, 24, 0, false, false);
        box(22, 107, 156, 7, false, true);
        if (s->prep_total)
            box(22, 107, 156 * s->prep_done / s->prep_total, 7, true, true);
        if (s->joint_active)
            snprintf(tmp, sizeof(tmp),
                     s->prep_phase == STAGE_PREP_WRITE ? "配置目标功率" : "缓存引闪波形");
        else
            preparation_detail(s, tmp, sizeof(tmp));
        centre_desc(100, 128, tmp, false);
        footer("操作已锁定");
        return;
    }
    if (s->page == STAGE_PAGE_HOME) {
        title(s, "灯光控制");
        button((stage_area_t){8, 44, 184, 52}, "引闪控制", 0);
        button((stage_area_t){8, 112, 184, 52}, "共同引闪", 0);
        footer("PWR 休眠");
        return;
    }
    if (s->page == STAGE_PAGE_TARGETS) {
        title(s, s->scanning ? "正在接收" : s->persist_error ? "保存失败" :
                 s->result ? "接收失败" : "已存组合");
        if (s->scanning) {
            snprintf(tmp, sizeof(tmp), "CH%02u  %us", s->scan_ch, s->scan_seconds);
            number(8, 29, tmp, 16, -1, false, false);
            snprintf(tmp, sizeof(tmp), s->target_count == 32 ? "已满" : "%u项", s->target_count);
            desc(152, 27, tmp, false);
        } else {
            button((stage_area_t){8, 26, 88, 22}, "手动添加", 0);
            button((stage_area_t){144, 26, 48, 22}, "清空", 0);
        }
        unsigned first = s->list_page * 4;
        for (unsigned row = 0; row < 4 && first + row < s->target_count; row++) {
            const stage_target_t *t = &s->targets[first + row];
            char id[8];
            if (t->id)
                snprintf(id, sizeof(id), "%02u", t->id);
            else
                snprintf(id, sizeof(id), "OFF");
            snprintf(tmp, sizeof(tmp), "CH%02u ID%s", t->ch, id);
            desc(8, 52 + row * 24, tmp, false);
            snprintf(tmp, sizeof(tmp), "%u", t->hits);
            number(190, 54 + row * 24, tmp, 16, 1, false, false);
        }
        if (!s->target_count) {
            centre_desc(100, 68, s->scanning ? "请逐个触发现场灯" : "尚无已存组合", false);
            centre_desc(100, 99, s->scanning ? "等待引闪信号" : "可手动添加组合", false);
        }
        button((stage_area_t){8, 152, 48, 24}, "上页", 0);
        button((stage_area_t){144, 152, 48, 24}, "下页", 0);
        snprintf(tmp, sizeof(tmp), "%u/%u", s->list_page + 1,
                 (s->target_count + 3) / 4 ? s->target_count / 4 + (s->target_count % 4 != 0) : 1);
        number(100, 156, tmp, 16, 0, false, false);
        footer(s->scanning ? "停止接收" : "返回共同引闪");
        return;
    }
    if (s->page == STAGE_PAGE_MANUAL && s->field < 0) {
        title(s, "手动添加");
        snprintf(tmp, sizeof(tmp), "已存 %u/%u", s->target_count, STAGE_TARGET_MAX);
        centre_desc(100, 28, tmp, false);
        value(tmp, sizeof(tmp), 0, s->manual.ch);
        card((stage_area_t){8, 66, 86, 40}, names[0], tmp);
        value(tmp, sizeof(tmp), 2, s->manual.id);
        card((stage_area_t){106, 66, 86, 40}, names[2], tmp);
        const char *status[] = {"点击卡片修改", "已保存，可继续添加", "组合已存在", "组合已满",
                                "保存失败，请重试"};
        centre_desc(100, 111, status[s->manual_status <= 4 ? s->manual_status : 0], false);
        button((stage_area_t){8, 137, 184, 30}, "添加组合", 0);
        footer("返回组合列表");
        return;
    }
    if (s->field >= 0) {
        int field = s->field;
        title(s, names[field]);
        if (field == 1 || field == 3) {
            centre_desc(100, 31, field == 1 ? "仅设置功率" : "手动功率", false);
            for (unsigned i = 0; i < (field == 1 ? 6 : 8); i++) {
                value(tmp, sizeof(tmp), field, i);
                button(stage_option_area(field, i), tmp,
                       BUTTON_VALUE | (s->draft == i ? BUTTON_SELECTED : 0));
            }
            if (field == 1)
                centre_desc(100, 151, "同CH/ID全部群闪", false);
        } else {
            const char *range[6] = {"1-32", "", "0-99", "", "1-999", ""};
            desc(8, 27, "范围", false);
            if (field == 5)
                snprintf(tmp, sizeof(tmp), "%u-%u", STAGE_INTERVAL_MIN_MS, STAGE_INTERVAL_MAX_MS);
            else
                snprintf(tmp, sizeof(tmp), "%s", range[field]);
            number(8, 46, tmp, 16, -1, false, false);
            if (!s->digits)
                snprintf(tmp, sizeof(tmp), "-");
            else if (field == 2 && !s->draft)
                snprintf(tmp, sizeof(tmp), "OFF");
            else
                snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)s->draft);
            number(192, 27, tmp, 32, 1, false, false);
            for (int i = 0; i < 12; i++) {
                bool numeric = true;
                if (i == 9) {
                    snprintf(tmp, sizeof(tmp), field == 2 ? "OFF" : "清空");
                    numeric = field == 2;
                } else if (i == 10)
                    snprintf(tmp, sizeof(tmp), "0");
                else if (i == 11) {
                    snprintf(tmp, sizeof(tmp), "退格");
                    numeric = false;
                } else
                    snprintf(tmp, sizeof(tmp), "%d", i + 1);
                button(stage_key_area(i), tmp, numeric ? BUTTON_VALUE : 0);
            }
        }
        button(stage_edit_action(false), "取消", 0);
        button(stage_edit_action(true), "确认",
               BUTTON_SELECTED | (stage_draft_valid(s) ? 0 : BUTTON_DISABLED));
    } else if (s->mode == STAGE_READY && s->page == STAGE_PAGE_JOINT) {
        title(s, "共同引闪");
        button((stage_area_t){8, 30, 88, 29}, "接收信号", 0);
        snprintf(tmp, sizeof(tmp), "已存 %u", s->target_count);
        button((stage_area_t){104, 30, 88, 29}, tmp, 0);
        const int fields[] = {3, 4, 5, 1};
        for (int i = 0; i < 4; i++) {
            int f = fields[i];
            value(tmp, sizeof(tmp), f, stage_config_value(&s->joint, f));
            card((stage_area_t){8 + i % 2 * 98, 82 + i / 2 * 50, 86, 40}, names[f], tmp);
        }
        footer(s->target_count ? "BOOT 共同引闪" : "请先添加或接收");
    } else if (s->mode == STAGE_READY) {
        title(s, "引闪控制");
        for (int field = 0; field < 6; field++) {
            value(tmp, sizeof(tmp), field, stage_config_value(&s->config, field));
            card(stage_home_card(field), names[field], tmp);
        }
        footer(s->cache_ready ? "BOOT 开始" : "BOOT 准备");
    } else if (s->legacy) {
        title(s, "串口测试");
        centre_desc(100, 65, s->mode == STAGE_RUNNING ? "执行中" : "已结束", false);
        centre_desc(100, 105, "CH08 / ID OFF", false);
        if (s->mode == STAGE_RUNNING)
            footer("停止 / BOOT");
        else {
            button(stage_return_area(), "返回参数", 0);
            footer("BOOT 重播");
        }
    } else if (!s->cache_ready && s->mode == STAGE_ERROR && !s->joint_active) {
        title(s, "准备失败");
        centre_desc(100, 65, "参数未就绪", false);
        snprintf(tmp, sizeof(tmp), "ERR 0x%X", (unsigned)s->result);
        centre_desc(100, 105, tmp, false);
        button(stage_return_area(), "返回参数", 0);
        footer("BOOT 重试");
    } else {
        const char *label = s->mode == STAGE_RUNNING   ? "正在群闪"
                            : s->mode == STAGE_DONE    ? "执行完成"
                            : s->mode == STAGE_STOPPED ? "已停止"
                                                       : "执行失败";
        title(s, label);
        desc(8, 29, s->joint_active ? "已发送轮次" : "已发送", false);
        if (s->timing_limited)
            desc(160, 29, "延迟", false);
        snprintf(tmp, sizeof(tmp), "%02u", s->sent);
        number(8, 49, tmp, 32, -1, false, false);
        snprintf(tmp, sizeof(tmp), "/ %u", s->run.count);
        number(192, 56, tmp, 24, 1, false, false);
        box(8, 91, 184, 7, false, true);
        if (s->run.count)
            box(8, 91, 184 * s->sent / s->run.count, 7, true, true);
        snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)s->run.interval_ms);
        card((stage_area_t){8, 113, 86, 40}, "间隔 ms", tmp);
        value(tmp, sizeof(tmp), 3, s->run.power);
        card((stage_area_t){106, 113, 86, 40}, "功率", tmp);
        if (s->mode == STAGE_RUNNING) {
            char id[8], group[8];
            value(id, sizeof(id), 2, s->run.id);
            value(group, sizeof(group), 1, s->run.group);
            if (s->joint_active)
                snprintf(tmp, sizeof(tmp), "%u组合 Group %s", s->target_count, group);
            else
                snprintf(tmp, sizeof(tmp), "CH%02u ID%s Group %s", s->run.ch, id, group);
            // Entire context line, including values, uses one QiuYe 16px font.
            centre_desc(100, 157, tmp, false);
            footer("停止 / BOOT");
        } else {
            button(stage_return_area(), "返回参数", 0);
            footer(s->cache_ready ? "BOOT 重播" : "BOOT 重试");
        }
    }
}
