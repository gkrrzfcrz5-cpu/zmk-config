/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Custom ZMK status screen for the AI Companion device — HOST-DRIVEN.
 *
 * MONO PROTOTYPE on the on-board 128x64 SSD1306 OLED, built while the colour
 * 1.9" ST7789 panel ships. Phase 5 wires this to the Mac: src/aic_comm.c reads
 * `screen` JSON from the USB CDC-ACM channel, fills a struct aic_screen_model,
 * and calls aic_screen_render() to draw it. Before the first message (and after
 * a disconnect) we show a local standby screen, so the panel is never blank/
 * garbage waiting for the host.
 *
 * The six host states map to docs/phase5-interface-contract.md §4.1:
 *   TASK / READY / PERMISSION / WAITING / STOPPED / DONE.
 * The mockup wording is Traditional Chinese, but the mono prototype's LVGL fonts
 * are Montserrat (Latin only), so FIXED labels here are short English/symbols;
 * the DYNAMIC fields (task / line / question) are drawn verbatim from the host
 * (English text renders; CJK glyphs come with the colour-panel font port).
 *
 * Thread-safety: LVGL is single-threaded. aic_screen_render() must run in the
 * ZMK display work-queue context; src/aic_comm.c guarantees that by dispatching
 * from a k_work submitted to zmk_display_work_q(). zmk_display_status_screen()
 * itself runs on that same queue (app/src/display/main.c init work), so the
 * shared s_model needs no extra lock.
 *
 * ZMK wiring (verified against zmkfirmware/zmk v0.3.0):
 *   - CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y makes ZMK call
 *     zmk_display_status_screen() (app/src/display/main.c) instead of the
 *     built-in widget screen; this strong definition overrides its weak stub.
 *   - LVGL is v8.3: lv_label, lv_bar and the LV_SYMBOL_* glyphs are available.
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <lvgl.h>

#include "aic_screen.h"

/* Declared by ZMK in app/include/zmk/display/status_screen.h. Declared locally
 * so this source does not depend on ZMK's PRIVATE app include path. */
lv_obj_t *zmk_display_status_screen(void);

/* Palette. On a 1bpp panel only black/white matter; the mockup is white-on-dark
 * so we draw white on black. If the panel shows up inverted, swap these two. */
#define AIC_FG lv_color_white()
#define AIC_BG lv_color_black()

static lv_obj_t *s_content;            /* full-screen container, cleared per draw */
static struct aic_screen_model s_model; /* last model from the host (zeroed = standby) */

/* --- small helpers --------------------------------------------------------- */

static lv_obj_t *aic_label(const char *text, const lv_font_t *font,
                           lv_align_t align, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, AIC_FG, LV_PART_MAIN);
    lv_obj_align(l, align, x, y);
    return l;
}

/* Word-wrapping label pinned to a top-left box the width of the panel. */
static lv_obj_t *aic_label_wrap(const char *text, const lv_font_t *font, lv_coord_t y)
{
    lv_obj_t *l = aic_label(text, font, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_set_width(l, 124);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

/* Top bar: STATE NAME (left) + a wifi glyph (right). */
static void aic_header(const char *title)
{
    aic_label(title, &lv_font_montserrat_12, LV_ALIGN_TOP_LEFT, 0, 0);
    aic_label(LV_SYMBOL_WIFI, &lv_font_montserrat_12, LV_ALIGN_TOP_RIGHT, 0, 0);
}

/* A bordered "chip" used for the Deny / Allow choices. */
static void aic_chip(const char *text, lv_align_t align)
{
    lv_obj_t *c = lv_label_create(s_content);
    lv_label_set_text(c, text);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(c, AIC_FG, LV_PART_MAIN);
    lv_obj_set_style_border_color(c, AIC_FG, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 2, LV_PART_MAIN);
    lv_obj_align(c, align, 0, 0);
}

/* Fall back to a placeholder when the host omitted a field. */
static const char *or_dash(const char *s)
{
    return (s && s[0]) ? s : "—";
}

/* --- draw one model -------------------------------------------------------- */

static void aic_draw(const struct aic_screen_model *m)
{
    lv_obj_clean(s_content);

    /* Empty state = local standby (pre-connect / disconnected). */
    if (m->state[0] == '\0') {
        aic_label("AI Companion", &lv_font_montserrat_12, LV_ALIGN_CENTER, 0, -6);
        aic_label("waiting for host", &lv_font_montserrat_10, LV_ALIGN_CENTER, 0, 10);
        return;
    }

    if (strcmp(m->state, "READY") == 0) {
        /* Clock standby: big clock, "READY" under it. */
        aic_label(or_dash(m->clock), &lv_font_montserrat_14, LV_ALIGN_CENTER, 0, -8);
        aic_label("READY", &lv_font_montserrat_12, LV_ALIGN_CENTER, 0, 12);
        return;
    }

    if (strcmp(m->state, "TASK") == 0) {
        /* Primary running task + its one-line status. */
        aic_header("RUNNING");
        aic_label_wrap(or_dash(m->task), &lv_font_montserrat_12, 16);
        aic_label_wrap(or_dash(m->line), &lv_font_montserrat_10, 38);
        return;
    }

    if (strcmp(m->state, "PERMISSION") == 0) {
        /* Task + question + Deny/Allow (No/Yes buttons). */
        aic_header("PERMISSION");
        aic_label_wrap(or_dash(m->task), &lv_font_montserrat_10, 15);
        aic_label_wrap(or_dash(m->question), &lv_font_montserrat_12, 28);
        aic_chip(LV_SYMBOL_CLOSE " Deny", LV_ALIGN_BOTTOM_LEFT);
        aic_chip(LV_SYMBOL_OK " Allow", LV_ALIGN_BOTTOM_RIGHT);
        return;
    }

    if (strcmp(m->state, "WAITING") == 0) {
        aic_header("WAITING");
        aic_label_wrap(or_dash(m->task), &lv_font_montserrat_12, 18);
        aic_label("waiting for you", &lv_font_montserrat_10, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return;
    }

    if (strcmp(m->state, "STOPPED") == 0) {
        aic_header("STOPPED");
        aic_label_wrap(or_dash(m->task), &lv_font_montserrat_12, 18);
        aic_label(LV_SYMBOL_WARNING " stopped", &lv_font_montserrat_10,
                  LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return;
    }

    if (strcmp(m->state, "DONE") == 0) {
        aic_header("DONE");
        aic_label_wrap(or_dash(m->task), &lv_font_montserrat_12, 18);
        aic_label(LV_SYMBOL_OK " done", &lv_font_montserrat_10,
                  LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return;
    }

    /* Unknown state (forward-compat): fall back to standby wording. */
    aic_label("AI Companion", &lv_font_montserrat_12, LV_ALIGN_CENTER, 0, -6);
    aic_label(or_dash(m->state), &lv_font_montserrat_10, LV_ALIGN_CENTER, 0, 10);
}

/* --- public entry point (called by src/aic_comm.c) ------------------------- */

void aic_screen_render(const struct aic_screen_model *model)
{
    s_model = *model;
    if (s_content != NULL) {
        aic_draw(&s_model);
    }
}

/* --- ZMK entry point ------------------------------------------------------- */

lv_obj_t *zmk_display_status_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, AIC_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Full-screen container with no border/radius and 1px padding so our
     * absolute coordinates map cleanly onto the 128x64 panel. */
    s_content = lv_obj_create(screen);
    lv_obj_set_size(s_content, 128, 64);
    lv_obj_align(s_content, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_content, AIC_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_content, 1, LV_PART_MAIN);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    /* Draw whatever we have (standby if the host hasn't spoken yet). */
    aic_draw(&s_model);

    return screen;
}
