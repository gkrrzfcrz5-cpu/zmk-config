/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Custom ZMK status screen for the AI Companion device — HOST-DRIVEN.
 *
 * ONE source drives BOTH panels, selected at compile time by the LVGL colour
 * depth the shield's .conf sets:
 *   - CONFIG_LV_COLOR_DEPTH_1  -> mono 128x64 SSD1306 OLED  (shield ai_companion)
 *   - CONFIG_LV_COLOR_DEPTH_16 -> colour 170x320 ST7789     (shield ai_companion_color)
 * The mono layout is the original eye-verified prototype and is kept verbatim in
 * the #else branch. The colour layout fills the whole panel, centres its text,
 * uses large Montserrat fonts, and tints the state with an accent colour.
 *
 * Phase 5 wires this to the Mac: src/aic_comm.c reads `screen` JSON from the USB
 * CDC-ACM channel, fills a struct aic_screen_model, and calls aic_screen_render()
 * to draw it. Before the first message (and after a disconnect) we show a local
 * standby screen, so the panel is never blank/garbage waiting for the host.
 *
 * The six host states map to docs/phase5-interface-contract.md §4.1:
 *   TASK / READY / PERMISSION / WAITING / STOPPED / DONE.
 * Four more (SESSIONS / LISTENING / PROCESSING / OPENING) are local-triggered
 * screens (device button / voice) echoed by the host — see §4.5.
 * FIXED labels are short English/symbols (LVGL Montserrat = Latin only); the
 * DYNAMIC fields (task / line / question) are drawn verbatim from the host.
 *
 * Thread-safety: LVGL is single-threaded. aic_screen_render() must run in the
 * ZMK display work-queue context; src/aic_comm.c guarantees that by dispatching
 * from a k_work submitted to zmk_display_work_q(). zmk_display_status_screen()
 * itself runs on that same queue (app/src/display/main.c init work), so the
 * shared s_model needs no extra lock.
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <lvgl.h>

#include "aic_screen.h"

/* Declared by ZMK in app/include/zmk/display/status_screen.h. Declared locally
 * so this source does not depend on ZMK's PRIVATE app include path. */
lv_obj_t *zmk_display_status_screen(void);

/* Colour build (170x320 ST7789) vs mono build (128x64 SSD1306). */
#if defined(CONFIG_LV_COLOR_DEPTH_16)
#define AIC_COLOR_UI 1
#endif

/* Foreground/background. Mono is 1bpp so only black/white matter; colour draws
 * white text on black and tints per state below. If mono shows up inverted, add
 * CONFIG_ZMK_DISPLAY_INVERT=y. */
#define AIC_FG lv_color_white()
#define AIC_BG lv_color_black()

/* Font tiers, sized for each panel. The draw code refers only to these macros,
 * so a build pulls in only the font sizes its .conf enables. */
#if defined(AIC_COLOR_UI)
#define AIC_F_HUGE  (&lv_font_montserrat_48)   /* clock / big glyph */
#define AIC_F_BIG   (&lv_font_montserrat_28)   /* primary line */
#define AIC_F_MED   (&lv_font_montserrat_20)   /* header / secondary */
#define AIC_F_SMALL (&lv_font_montserrat_16)   /* footer note */
#else
#define AIC_F_BIG   (&lv_font_montserrat_14)
#define AIC_F_MED   (&lv_font_montserrat_12)
#define AIC_F_SMALL (&lv_font_montserrat_10)
#endif

static lv_obj_t *s_content;            /* full-screen container, cleared per draw */
static struct aic_screen_model s_model; /* last model from the host (zeroed = standby) */

/* --- shared helpers (both panels) ------------------------------------------ */

/* Fall back to a placeholder when the host omitted a field. */
static const char *or_dash(const char *s)
{
    return (s && s[0]) ? s : "—";
}

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

/* A bordered "chip" used for the Deny / Allow choices (bottom corners). */
static void aic_chip_f(const char *text, const lv_font_t *font, lv_align_t align)
{
    lv_obj_t *c = lv_label_create(s_content);
    lv_label_set_text(c, text);
    lv_obj_set_style_text_font(c, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(c, AIC_FG, LV_PART_MAIN);
    lv_obj_set_style_border_color(c, AIC_FG, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 2, LV_PART_MAIN);
    lv_obj_align(c, align, 0, 0);
}

#if defined(AIC_COLOR_UI)
/* ======================= COLOUR 170x320 layout ============================= */

#define AIC_C_WHITE  lv_color_white()
#define AIC_C_GREY   lv_color_hex(0x94A3B8)
#define AIC_C_GREEN  lv_color_hex(0x22C55E)
#define AIC_C_CYAN   lv_color_hex(0x38BDF8)
#define AIC_C_AMBER  lv_color_hex(0xF59E0B)
#define AIC_C_RED    lv_color_hex(0xEF4444)
#define AIC_C_PURPLE lv_color_hex(0xA78BFA)

#define AIC_COL(l, c) lv_obj_set_style_text_color((l), (c), LV_PART_MAIN)

/* Centred, colour-tinted state header pinned to the top. */
static void aic_header_c(const char *title, lv_color_t accent)
{
    lv_obj_t *l = aic_label(title, AIC_F_MED, LV_ALIGN_TOP_MID, 0, 10);
    AIC_COL(l, accent);
}

/* Word-wrapping, centre-aligned label placed in the middle band of the panel. */
static lv_obj_t *aic_wrap_c(const char *text, const lv_font_t *font,
                            lv_coord_t y, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, col, LV_PART_MAIN);
    lv_obj_set_width(l, lv_disp_get_hor_res(NULL) - 16);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

static void aic_draw(const struct aic_screen_model *m)
{
    lv_obj_clean(s_content);

    /* Empty state = local standby (pre-connect / disconnected). */
    if (m->state[0] == '\0') {
        aic_label("AI Companion", AIC_F_BIG, LV_ALIGN_CENTER, 0, -18);
        AIC_COL(aic_label("waiting for host", AIC_F_SMALL, LV_ALIGN_CENTER, 0, 22),
                AIC_C_GREY);
        return;
    }

    if (strcmp(m->state, "READY") == 0) {
        aic_label(or_dash(m->clock), AIC_F_HUGE, LV_ALIGN_CENTER, 0, -24);
        AIC_COL(aic_label("READY", AIC_F_MED, LV_ALIGN_CENTER, 0, 40), AIC_C_GREEN);
        return;
    }

    if (strcmp(m->state, "TASK") == 0) {
        aic_header_c("RUNNING", AIC_C_CYAN);
        aic_wrap_c(or_dash(m->task), AIC_F_BIG, -10, AIC_C_WHITE);
        AIC_COL(aic_label(or_dash(m->line), AIC_F_SMALL, LV_ALIGN_CENTER, 0, 54),
                AIC_C_GREY);
        return;
    }

    if (strcmp(m->state, "PERMISSION") == 0) {
        aic_header_c("PERMISSION", AIC_C_AMBER);
        aic_wrap_c(or_dash(m->question), AIC_F_BIG, -20, AIC_C_WHITE);
        AIC_COL(aic_label(or_dash(m->task), AIC_F_SMALL, LV_ALIGN_CENTER, 0, 34),
                AIC_C_GREY);
        aic_chip_f(LV_SYMBOL_CLOSE " Deny", AIC_F_MED, LV_ALIGN_BOTTOM_LEFT);
        aic_chip_f(LV_SYMBOL_OK " Allow", AIC_F_MED, LV_ALIGN_BOTTOM_RIGHT);
        return;
    }

    if (strcmp(m->state, "WAITING") == 0) {
        aic_header_c("WAITING", AIC_C_AMBER);
        aic_wrap_c(or_dash(m->task), AIC_F_BIG, -10, AIC_C_WHITE);
        AIC_COL(aic_label("waiting for you", AIC_F_SMALL, LV_ALIGN_BOTTOM_MID, 0, -12),
                AIC_C_AMBER);
        return;
    }

    if (strcmp(m->state, "STOPPED") == 0) {
        aic_header_c("STOPPED", AIC_C_RED);
        aic_wrap_c(or_dash(m->task), AIC_F_BIG, -10, AIC_C_WHITE);
        AIC_COL(aic_label(LV_SYMBOL_WARNING " stopped", AIC_F_SMALL,
                          LV_ALIGN_BOTTOM_MID, 0, -12), AIC_C_RED);
        return;
    }

    if (strcmp(m->state, "DONE") == 0) {
        aic_header_c("DONE", AIC_C_GREEN);
        aic_wrap_c(or_dash(m->task), AIC_F_BIG, -10, AIC_C_WHITE);
        AIC_COL(aic_label(LV_SYMBOL_OK " done", AIC_F_SMALL,
                          LV_ALIGN_BOTTOM_MID, 0, -12), AIC_C_GREEN);
        return;
    }

    if (strcmp(m->state, "SESSIONS") == 0) {
        aic_header_c("SESSIONS", AIC_C_CYAN);
        aic_wrap_c(or_dash(m->task), AIC_F_BIG, -10, AIC_C_WHITE);
        AIC_COL(aic_label(LV_SYMBOL_LIST " your tasks", AIC_F_SMALL,
                          LV_ALIGN_BOTTOM_MID, 0, -12), AIC_C_CYAN);
        return;
    }

    if (strcmp(m->state, "LISTENING") == 0) {
        aic_header_c("LISTENING", AIC_C_PURPLE);
        AIC_COL(aic_label(LV_SYMBOL_AUDIO, AIC_F_HUGE, LV_ALIGN_CENTER, 0, -20),
                AIC_C_PURPLE);
        aic_label("listening...", AIC_F_MED, LV_ALIGN_CENTER, 0, 44);
        return;
    }

    if (strcmp(m->state, "PROCESSING") == 0) {
        aic_header_c("PROCESSING", AIC_C_PURPLE);
        AIC_COL(aic_label(LV_SYMBOL_REFRESH, AIC_F_HUGE, LV_ALIGN_CENTER, 0, -20),
                AIC_C_PURPLE);
        aic_label("thinking...", AIC_F_MED, LV_ALIGN_CENTER, 0, 44);
        return;
    }

    if (strcmp(m->state, "OPENING") == 0) {
        aic_header_c("OPENING", AIC_C_PURPLE);
        aic_wrap_c(or_dash(m->task), AIC_F_BIG, -10, AIC_C_WHITE);
        AIC_COL(aic_label(LV_SYMBOL_EYE_OPEN " opening...", AIC_F_SMALL,
                          LV_ALIGN_BOTTOM_MID, 0, -12), AIC_C_PURPLE);
        return;
    }

    /* Unknown state (forward-compat): fall back to standby wording. */
    aic_label("AI Companion", AIC_F_BIG, LV_ALIGN_CENTER, 0, -18);
    AIC_COL(aic_label(or_dash(m->state), AIC_F_SMALL, LV_ALIGN_CENTER, 0, 22),
            AIC_C_GREY);
}

#else
/* ======================= MONO 128x64 layout (verbatim) ===================== */

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
    aic_label(title, AIC_F_MED, LV_ALIGN_TOP_LEFT, 0, 0);
    aic_label(LV_SYMBOL_WIFI, AIC_F_MED, LV_ALIGN_TOP_RIGHT, 0, 0);
}

static void aic_draw(const struct aic_screen_model *m)
{
    lv_obj_clean(s_content);

    /* Empty state = local standby (pre-connect / disconnected). */
    if (m->state[0] == '\0') {
        aic_label("AI Companion", AIC_F_MED, LV_ALIGN_CENTER, 0, -6);
        aic_label("waiting for host", AIC_F_SMALL, LV_ALIGN_CENTER, 0, 10);
        return;
    }

    if (strcmp(m->state, "READY") == 0) {
        /* Clock standby: big clock, "READY" under it. */
        aic_label(or_dash(m->clock), AIC_F_BIG, LV_ALIGN_CENTER, 0, -8);
        aic_label("READY", AIC_F_MED, LV_ALIGN_CENTER, 0, 12);
        return;
    }

    if (strcmp(m->state, "TASK") == 0) {
        /* Primary running task + its one-line status. */
        aic_header("RUNNING");
        aic_label_wrap(or_dash(m->task), AIC_F_MED, 16);
        aic_label_wrap(or_dash(m->line), AIC_F_SMALL, 38);
        return;
    }

    if (strcmp(m->state, "PERMISSION") == 0) {
        /* Task + question + Deny/Allow (No/Yes buttons). */
        aic_header("PERMISSION");
        aic_label_wrap(or_dash(m->task), AIC_F_SMALL, 15);
        aic_label_wrap(or_dash(m->question), AIC_F_MED, 28);
        aic_chip_f(LV_SYMBOL_CLOSE " Deny", AIC_F_MED, LV_ALIGN_BOTTOM_LEFT);
        aic_chip_f(LV_SYMBOL_OK " Allow", AIC_F_MED, LV_ALIGN_BOTTOM_RIGHT);
        return;
    }

    if (strcmp(m->state, "WAITING") == 0) {
        aic_header("WAITING");
        aic_label_wrap(or_dash(m->task), AIC_F_MED, 18);
        aic_label("waiting for you", AIC_F_SMALL, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return;
    }

    if (strcmp(m->state, "STOPPED") == 0) {
        aic_header("STOPPED");
        aic_label_wrap(or_dash(m->task), AIC_F_MED, 18);
        aic_label(LV_SYMBOL_WARNING " stopped", AIC_F_SMALL,
                  LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return;
    }

    if (strcmp(m->state, "DONE") == 0) {
        aic_header("DONE");
        aic_label_wrap(or_dash(m->task), AIC_F_MED, 18);
        aic_label(LV_SYMBOL_OK " done", AIC_F_SMALL,
                  LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return;
    }

    /* Local-triggered screens (device button / voice, echoed by the host so the
     * host stays source of truth). See docs/phase5-interface-contract.md §4.5. */
    if (strcmp(m->state, "SESSIONS") == 0) {
        /* Task list summary (Open/encoder browse). */
        aic_header("SESSIONS");
        aic_label_wrap(or_dash(m->task), AIC_F_MED, 18);
        aic_label(LV_SYMBOL_LIST " your tasks", AIC_F_SMALL,
                  LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return;
    }

    if (strcmp(m->state, "LISTENING") == 0) {
        /* Voice capture in progress (Voice button). */
        aic_header("LISTENING");
        aic_label(LV_SYMBOL_AUDIO " listening...", AIC_F_MED,
                  LV_ALIGN_CENTER, 0, 6);
        return;
    }

    if (strcmp(m->state, "PROCESSING") == 0) {
        /* Voice understood, thinking. */
        aic_header("PROCESSING");
        aic_label(LV_SYMBOL_REFRESH " thinking...", AIC_F_MED,
                  LV_ALIGN_CENTER, 0, 6);
        return;
    }

    if (strcmp(m->state, "OPENING") == 0) {
        /* Opening the result (Open button). */
        aic_header("OPENING");
        aic_label_wrap(or_dash(m->task), AIC_F_MED, 18);
        aic_label(LV_SYMBOL_EYE_OPEN " opening...", AIC_F_SMALL,
                  LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return;
    }

    /* Unknown state (forward-compat): fall back to standby wording. */
    aic_label("AI Companion", AIC_F_MED, LV_ALIGN_CENTER, 0, -6);
    aic_label(or_dash(m->state), AIC_F_SMALL, LV_ALIGN_CENTER, 0, 10);
}

#endif /* AIC_COLOR_UI */

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

    /* Full-screen container with no border/radius. On colour it fills the whole
     * panel (queried at runtime) so content centres on 170x320 instead of being
     * pinned to a 128x64 box in the corner; on mono it is the 128x64 panel. */
    s_content = lv_obj_create(screen);
#if defined(AIC_COLOR_UI)
    lv_obj_set_size(s_content, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
#else
    lv_obj_set_size(s_content, 128, 64);
#endif
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
