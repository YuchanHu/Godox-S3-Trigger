/* SPDX-License-Identifier: GPL-3.0-or-later
 * Documentation frames use the same model, renderer and glyphs as the firmware.
 * Run from tools/render_ui.py. Values are illustrative UI states.
 */
#include "stage_view.h"
#include <stdio.h>

static int save(const char *name, const stage_ui_t *state) {
    uint8_t frame[STAGE_FRAME_BYTES];
    stage_view_render(state, frame);
    FILE *file = fopen(name, "wb");
    if (!file)
        return 1;
    /* PBM uses one for black, opposite to the panel framebuffer. */
    for (unsigned i = 0; i < sizeof(frame); i++)
        frame[i] ^= 255;
    int failed = fprintf(file, "P4\n200 200\n") < 0;
    failed |= fwrite(frame, 1, sizeof(frame), file) != sizeof(frame);
    failed |= fclose(file) != 0;
    return failed;
}

int main(void) {
    stage_ui_t state;
    stage_core_init(&state);
    state.battery_valid = true;
    state.battery_mv = 4000;
    state.battery_percent = 85;
    state.cache_ready = true;
    state.page = STAGE_PAGE_HOME;
    if (save("home.pbm", &state))
        return 1;
    state.page = STAGE_PAGE_SINGLE;
    if (save("control.pbm", &state))
        return 1;
    stage_ui_t editor = state;
    stage_core_touch(&editor, 20, 45);
    if (save("editor.pbm", &editor))
        return 1;
    const char *edit_names[] = {"group.pbm", "id.pbm", "power.pbm", "count.pbm", "interval.pbm"};
    const int edit_points[][2] = {{120, 45}, {20, 95}, {120, 95}, {20, 145}, {120, 145}};
    for (unsigned i = 0; i < 5; i++) {
        editor = state;
        stage_core_touch(&editor, edit_points[i][0], edit_points[i][1]);
        if (save(edit_names[i], &editor))
            return 1;
    }
    stage_ui_t preparing = state;
    preparing.preparing = true;
    preparing.prep_phase = STAGE_PREP_COMPUTE;
    preparing.prep_wave = STAGE_PREP_WAVE_POWER;
    preparing.prep_group = 1;
    preparing.prep_done = 3;
    preparing.prep_total = 9;
    if (save("preparing.pbm", &preparing))
        return 1;
    state.page = STAGE_PAGE_JOINT;
    stage_target_add(&state, 14, 0);
    stage_target_add(&state, 8, 12);
    stage_target_add(&state, 22, 37);
    if (save("joint.pbm", &state))
        return 1;
    state.page = STAGE_PAGE_TARGETS;
    state.scanning = true;
    state.scan_ch = 14;
    state.scan_seconds = 86;
    if (save("scan.pbm", &state))
        return 1;
    stage_ui_t empty = state;
    empty.target_count = 0;
    if (save("scan-empty.pbm", &empty))
        return 1;
    state.scanning = false;
    stage_ui_t targets = state;
    stage_target_add(&targets, 24, 5);
    stage_target_add(&targets, 32, 99);
    if (save("targets.pbm", &targets))
        return 1;
    stage_ui_t manual = targets;
    stage_core_touch(&manual, 40, 35);
    if (save("manual.pbm", &manual))
        return 1;
    manual.manual.ch = 11;
    manual.manual.id = 0;
    if (!stage_manual_add(&manual))
        return 1;
    if (save("manual-saved.pbm", &manual))
        return 1;
    stage_manual_add(&manual);
    if (save("manual-duplicate.pbm", &manual))
        return 1;
    stage_core_touch(&targets, 165, 160);
    if (save("targets-page2.pbm", &targets))
        return 1;
    empty.scanning = false;
    if (save("targets-empty.pbm", &empty))
        return 1;
    state.page = STAGE_PAGE_SINGLE;
    state.run = state.config;
    state.mode = STAGE_RUNNING;
    state.sent = 3;
    if (save("running.pbm", &state))
        return 1;
    stage_ui_t joint_run = state;
    joint_run.page = STAGE_PAGE_JOINT;
    joint_run.joint_active = true;
    joint_run.run = joint_run.joint;
    if (save("joint-running.pbm", &joint_run))
        return 1;
    state.mode = STAGE_DONE;
    state.sent = state.run.count;
    if (save("done.pbm", &state))
        return 1;
    state.mode = STAGE_STOPPED;
    state.sent = 3;
    if (save("stopped.pbm", &state))
        return 1;
    state.mode = STAGE_ERROR;
    state.cache_ready = false;
    state.result = 0x101;
    if (save("prepare-error.pbm", &state))
        return 1;
    state.sleep_requested = true;
    return save("sleep.pbm", &state);
}
