/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define STAGE_INTERVAL_MIN_MS 1u
#define STAGE_INTERVAL_MAX_MS 60000u
#define STAGE_TARGET_MAX 32
typedef struct {
    uint8_t ch, id;
    uint16_t hits;
} stage_target_t;
typedef enum {
    STAGE_PAGE_SINGLE,
    STAGE_PAGE_HOME,
    STAGE_PAGE_JOINT,
    STAGE_PAGE_TARGETS,
    STAGE_PAGE_MANUAL
} stage_page_t;
typedef struct {
    uint32_t interval_ms;
    uint16_t count;
    uint8_t ch, group, id, power; // group=0 ALL, 1..5 A..E; power=0 1/128 ..7 1/1
} stage_config_t;
typedef enum { STAGE_READY, STAGE_RUNNING, STAGE_DONE, STAGE_STOPPED, STAGE_ERROR } stage_mode_t;
typedef enum {
    STAGE_PREP_START,
    STAGE_PREP_CHECK,
    STAGE_PREP_ERASE,
    STAGE_PREP_COMPUTE,
    STAGE_PREP_WRITE,
    STAGE_PREP_HEADER,
    STAGE_PREP_VERIFY
} stage_prep_phase_t;
typedef enum {
    STAGE_PREP_WAVE_NONE,
    STAGE_PREP_WAVE_PREPARE,
    STAGE_PREP_WAVE_MODE,
    STAGE_PREP_WAVE_POWER,
    STAGE_PREP_WAVE_TEST,
    STAGE_PREP_WAVE_END
} stage_prep_wave_t;
typedef struct {
    stage_config_t config, run;
    stage_mode_t mode;
    int field; // -1 none; CH/group/ID/power/count/interval = 0..5
    uint32_t draft;
    uint8_t digits;
    bool fresh;
    uint16_t sent;
    int result;
    bool timing_limited;
    bool legacy;
    bool preparing;
    bool cache_ready;
    uint8_t prep_done, prep_total;
    bool battery_valid;
    int16_t battery_mv, battery_percent;
    uint32_t first_test_delay_ms;
    uint32_t prep_generation;
    uint8_t prep_phase, prep_wave, prep_group;
    int page;
    stage_config_t joint;
    stage_target_t targets[STAGE_TARGET_MAX];
    uint8_t target_count, list_page, scan_ch;
    bool scanning, sleep_requested, joint_active;
    uint16_t scan_seconds;
    uint32_t scan_captures, scan_errors, persist_error;
    stage_config_t manual;
    uint8_t manual_status; // 0 edit, 1 saved, 2 duplicate, 3 full, 4 save failed
} stage_ui_t;
typedef enum {
    STAGE_NO_ACTION,
    STAGE_SAVE,
    STAGE_START,
    STAGE_CANCEL_RUN,
    STAGE_SCAN,
    STAGE_CLEAR_TARGETS,
    STAGE_ADD_TARGET,
    STAGE_RETURN_HOME,
    STAGE_SLEEP
} stage_action_t;
stage_config_t *stage_edit_config(stage_ui_t *s);
bool stage_target_add(stage_ui_t *s, unsigned ch, unsigned id);
bool stage_manual_add(stage_ui_t *s);
void stage_core_init(stage_ui_t *s);
bool stage_config_valid(const stage_config_t *c);
bool stage_draft_valid(const stage_ui_t *s);
stage_action_t stage_core_touch(stage_ui_t *s, int x, int y);
stage_action_t stage_core_boot(stage_ui_t *s);
stage_action_t stage_core_power(const stage_ui_t *s);
void stage_core_home(stage_ui_t *s);
uint32_t stage_config_value(const stage_config_t *c, int field);
int stage_battery_percent(int millivolts);
