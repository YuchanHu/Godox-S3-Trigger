/* SPDX-License-Identifier: GPL-3.0-or-later; no OS/hardware dependencies. */
#include "stage_core.h"
#include "stage_layout.h"
stage_config_t *stage_edit_config(stage_ui_t *s) {
    if (s->page == STAGE_PAGE_MANUAL)
        return &s->manual;
    return s->page == STAGE_PAGE_JOINT ? &s->joint : &s->config;
}
bool stage_manual_add(stage_ui_t *s) {
    if (s->manual.ch < 1 || s->manual.ch > 32 || s->manual.id > 99)
        return false;
    for (unsigned i = 0; i < s->target_count; i++) {
        if (s->targets[i].ch == s->manual.ch && s->targets[i].id == s->manual.id) {
            s->manual_status = 2;
            return false;
        }
    }
    if (s->target_count == STAGE_TARGET_MAX) {
        s->manual_status = 3;
        return false;
    }
    stage_target_add(s, s->manual.ch, s->manual.id);
    s->targets[s->target_count - 1].hits = 0;
    s->list_page = (s->target_count - 1) / 4;
    s->manual_status = 1;
    return true;
}
bool stage_target_add(stage_ui_t *s, unsigned ch, unsigned id) {
    if (ch < 1 || ch > 32 || id > 99)
        return false;
    for (unsigned i = 0; i < s->target_count; i++)
        if (s->targets[i].ch == ch && s->targets[i].id == id) {
            if (s->targets[i].hits < 65535)
                s->targets[i].hits++;
            return false;
        }
    if (s->target_count >= STAGE_TARGET_MAX)
        return false;
    s->targets[s->target_count++] = (stage_target_t){ch, id, 1};
    return true;
}
static const uint32_t low[6] = {1, 0, 0, 0, 1, STAGE_INTERVAL_MIN_MS},
                      high[6] = {32, 5, 99, 7, 999, STAGE_INTERVAL_MAX_MS};
uint32_t stage_config_value(const stage_config_t *c, int f) {
    switch (f) {
    case 0:
        return c->ch;
    case 1:
        return c->group;
    case 2:
        return c->id;
    case 3:
        return c->power;
    case 4:
        return c->count;
    default:
        return c->interval_ms;
    }
}
static void set_value(stage_config_t *c, int f, uint32_t v) {
    switch (f) {
    case 0:
        c->ch = v;
        break;
    case 1:
        c->group = v;
        break;
    case 2:
        c->id = v;
        break;
    case 3:
        c->power = v;
        break;
    case 4:
        c->count = v;
        break;
    case 5:
        c->interval_ms = v;
        break;
    }
}
bool stage_config_valid(const stage_config_t *c) {
    if (!c)
        return false;
    for (int f = 0; f < 6; f++) {
        uint32_t v = stage_config_value(c, f);
        if (v < low[f] || v > high[f])
            return false;
    }
    return true;
}
void stage_core_init(stage_ui_t *s) {
    *s = (stage_ui_t){
        .config = {.interval_ms = 1000, .count = 10, .ch = 8, .group = 0, .id = 0, .power = 1},
        .mode = STAGE_READY,
        .field = -1,
        .battery_percent = -1};
    s->joint = s->config;
    s->manual = s->config;
}
int stage_battery_percent(int millivolts) {
    if (millivolts < 2500 || millivolts > 4500)
        return -1;
    if (millivolts <= 3000)
        return 0;
    if (millivolts >= 4120)
        return 100;
    return (millivolts - 3000) * 100 / 1120;
}
bool stage_draft_valid(const stage_ui_t *s) {
    return s->field >= 0 && s->field < 6 && s->digits && s->draft >= low[s->field] &&
           s->draft <= high[s->field];
}
stage_action_t stage_core_power(const stage_ui_t *s) {
    if (s->sleep_requested)
        return STAGE_NO_ACTION;
    if (s->page == STAGE_PAGE_HOME && s->mode == STAGE_READY && !s->preparing && !s->scanning)
        return STAGE_SLEEP;
    return STAGE_RETURN_HOME;
}
void stage_core_home(stage_ui_t *s) {
    s->page = STAGE_PAGE_HOME;
    s->mode = STAGE_READY;
    s->field = -1;
    s->preparing = false;
    s->scanning = false;
    s->scan_seconds = 0;
    s->joint_active = false;
    s->legacy = false;
}
stage_action_t stage_core_boot(stage_ui_t *s) {
    if (s->preparing || s->sleep_requested)
        return STAGE_NO_ACTION;
    if (s->mode == STAGE_RUNNING)
        return STAGE_CANCEL_RUN;
    if (s->scanning)
        return STAGE_CANCEL_RUN;
    if (s->page == STAGE_PAGE_HOME || s->page == STAGE_PAGE_TARGETS ||
        s->page == STAGE_PAGE_MANUAL)
        return STAGE_NO_ACTION;
    if (s->page == STAGE_PAGE_JOINT && !s->target_count)
        return STAGE_NO_ACTION;
    if (s->field >= 0 || !stage_config_valid(stage_edit_config(s)))
        return STAGE_NO_ACTION;
    return STAGE_START;
}
stage_action_t stage_core_touch(stage_ui_t *s, int x, int y) {
    if (s->preparing || s->sleep_requested)
        return STAGE_NO_ACTION;
    if (x < 0 || x >= 200 || y < 0 || y >= 200)
        return STAGE_NO_ACTION;
    if (s->mode == STAGE_RUNNING)
        return stage_area_contains(stage_stop_area(), x, y) ? STAGE_CANCEL_RUN : STAGE_NO_ACTION;
    if (s->page == STAGE_PAGE_MANUAL && s->field < 0) {
        if (y >= 180) {
            s->page = STAGE_PAGE_TARGETS;
        } else if (y >= 131 && y < 169 && x >= 8 && x < 192) {
            return STAGE_ADD_TARGET;
        } else if (y >= 51 && y < 111 && x >= 8 && x < 192) {
            s->field = x < 100 ? 0 : 2;
            s->draft = stage_config_value(&s->manual, s->field);
            s->digits = s->draft >= 10 ? 2 : 1;
            s->fresh = true;
            s->manual_status = 0;
        }
        return STAGE_NO_ACTION;
    }
    if (s->field < 0 && s->page == STAGE_PAGE_HOME) {
        if (x >= 8 && x < 192 && y >= 44 && y < 96)
            s->page = STAGE_PAGE_SINGLE;
        if (x >= 8 && x < 192 && y >= 112 && y < 164)
            s->page = STAGE_PAGE_JOINT;
        return STAGE_NO_ACTION;
    }
    if (s->field < 0 && s->page == STAGE_PAGE_TARGETS) {
        if (y >= 180) {
            if (s->scanning)
                return STAGE_CANCEL_RUN;
            s->page = STAGE_PAGE_JOINT;
        } else if (y >= 152 && !s->scanning) {
            if (x < 60 && s->list_page)
                s->list_page--;
            else if (x >= 140 && (s->list_page + 1) * 4 < s->target_count)
                s->list_page++;
        } else if (y >= 152 && s->scanning) {
            if (x < 100 && s->list_page)
                s->list_page--;
            else if (x >= 100 && (s->list_page + 1) * 4 < s->target_count)
                s->list_page++;
        } else if (y >= 25 && y < 49 && !s->scanning) {
            if (x >= 144)
                return STAGE_CLEAR_TARGETS;
            if (x >= 8 && x < 96) {
                s->page = STAGE_PAGE_MANUAL;
                s->manual_status = 0;
            }
        }
        return STAGE_NO_ACTION;
    }
    if (s->field < 0 && s->mode == STAGE_READY && y < 24 && x < 100) {
        s->page = STAGE_PAGE_HOME;
        return STAGE_NO_ACTION;
    }
    if (s->field < 0 && s->mode == STAGE_READY && s->page == STAGE_PAGE_JOINT) {
        if (y >= 30 && y < 59) {
            if (x < 100)
                return STAGE_SCAN;
            s->page = STAGE_PAGE_TARGETS;
            return STAGE_NO_ACTION;
        }
        const int fields[] = {3, 4, 5, 1};
        for (int i = 0; i < 4; i++) {
            stage_area_t a = {8 + i % 2 * 98, 72 + i / 2 * 50, 86, 50};
            if (stage_area_contains(a, x, y)) {
                s->field = fields[i];
                s->draft = stage_config_value(&s->joint, s->field);
                s->digits = 1;
                for (uint32_t v = s->draft; v >= 10; v /= 10)
                    s->digits++;
                s->fresh = true;
                break;
            }
        }
        return STAGE_NO_ACTION;
    }
    if (s->field < 0) {
        if (s->mode != STAGE_READY) {
            if (stage_area_contains(stage_return_area(), x, y)) {
                s->mode = STAGE_READY;
                s->legacy = false;
            }
            return STAGE_NO_ACTION;
        }
        for (int f = 0; f < 6; f++) {
            if (stage_area_contains(stage_home_hit(f), x, y)) {
                s->field = f;
                s->draft = stage_config_value(stage_edit_config(s), f);
                s->digits = 1;
                for (uint32_t v = s->draft; v >= 10; v /= 10)
                    s->digits++;
                s->fresh = true;
                break;
            }
        }
        return STAGE_NO_ACTION;
    }
    if (stage_area_contains(stage_edit_action(false), x, y)) {
        s->field = -1;
        return STAGE_NO_ACTION;
    }
    if (stage_area_contains(stage_edit_action(true), x, y)) {
        if (stage_draft_valid(s)) {
            set_value(stage_edit_config(s), s->field, s->draft);
            s->field = -1;
            s->mode = STAGE_READY;
            return s->page == STAGE_PAGE_MANUAL ? STAGE_NO_ACTION : STAGE_SAVE;
        }
        return STAGE_NO_ACTION;
    }
    if (s->field == 1 || s->field == 3) {
        for (int idx = 0; idx < (s->field == 1 ? 6 : 8); idx++)
            if (stage_area_contains(stage_option_area(s->field, idx), x, y)) {
                s->draft = idx;
                s->digits = 1;
                break;
            }
    } else {
        int key = -1;
        for (int i = 0; i < 12; i++)
            if (stage_area_contains(stage_key_area(i), x, y)) {
                key = i;
                break;
            }
        if (key < 0)
            return STAGE_NO_ACTION;
        if (key == 9) {
            s->draft = 0;
            s->digits = s->field == 2 ? 1 : 0;
            s->fresh = false;
        } else if (key == 11) {
            s->draft /= 10;
            if (s->digits)
                s->digits--;
            s->fresh = false;
        } else {
            uint32_t digit = key == 10 ? 0 : key + 1;
            if (s->fresh) {
                s->draft = 0;
                s->digits = 0;
                s->fresh = false;
            }
            unsigned max_digits = s->field == 5 ? 5 : (s->field == 4 ? 3 : 2);
            if (s->digits < max_digits) {
                s->draft = s->draft * 10 + digit;
                s->digits++;
            }
        }
    }
    return STAGE_NO_ACTION;
}
