/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Shared screen model between the host-communication module (src/aic_comm.c,
 * which fills it from incoming `screen` JSON) and the LVGL renderer
 * (src/status_screen.c, which draws it). See docs/phase5-interface-contract.md
 * for the message semantics; the field sizes here are the device-side caps.
 */

#pragma once

#include <stddef.h>

/* Longest field text we keep. The panel can only show a short line anyway, so
 * anything longer is truncated on the way in. */
#define AIC_STATE_LEN    16
#define AIC_TASK_LEN     40
#define AIC_LINE_LEN     40
#define AIC_QUESTION_LEN 64
#define AIC_CLOCK_LEN    8
#define AIC_ID_LEN       40

/*
 * One rendered screen. `state` is the host's `screen.state` string verbatim:
 * "TASK" / "READY" / "PERMISSION" / "WAITING" / "STOPPED" / "DONE". An empty
 * `state` (the power-on / disconnected default) draws the local standby screen.
 * `id` is not drawn; it is retained so the button handler (task 2) can bind the
 * user's reply to the right item.
 */
struct aic_screen_model {
    char state[AIC_STATE_LEN];
    char task[AIC_TASK_LEN];
    char line[AIC_LINE_LEN];
    char question[AIC_QUESTION_LEN];
    char clock[AIC_CLOCK_LEN];
    char id[AIC_ID_LEN];
};

/*
 * Apply a new screen model and redraw. MUST be called from the ZMK display work
 * queue context (that is where LVGL is serviced); src/aic_comm.c does exactly
 * that by dispatching from a k_work submitted to zmk_display_work_q(). Safe to
 * call before the screen is built — the model is stored and drawn once the
 * status screen initialises.
 */
void aic_screen_render(const struct aic_screen_model *model);
