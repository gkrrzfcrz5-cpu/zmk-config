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

#if defined(AIC_COLOR_UI)
/* ================= COLOUR 320x170 landscape layout ========================= */
/*
 * Design language (matches the "12 Core Screen States" mockup): ONE warm-orange
 * accent on near-black, a top status bar (logo square + STATE + signal bars), a
 * robot mascot with per-state expressions, and a bottom status line. Everything
 * is built from plain rounded-rect objects (no extra LVGL features to enable)
 * plus labels, so it stays portable and cheap.
 *
 * NOTE: the count/list/progress-bar richness of the mockup (HOME's 3 counters,
 * the SESSIONS list, WORKING's % bar, the PERMISSIONS queue) needs data the host
 * does not send yet; those screens render a faithful single-item version for now
 * and get wired to real data once the JSON contract is extended (Phase B).
 */

#define AC_ACCENT lv_color_hex(0xF5551E)   /* the one accent: warm orange */
#define AC_WHITE  lv_color_white()
#define AC_GREY   lv_color_hex(0x9096A0)    /* secondary text / labels */
#define AC_BARS   lv_color_hex(0xC8CDD4)    /* signal bars */
#define AC_ROW    lv_color_hex(0x1C2026)    /* highlighted row / Deny button / icon bg */
#define AC_EYE    lv_color_hex(0x14181E)    /* mascot features on the white face */

/* Our single drawing primitive: a styled rectangle (no border/pad/scrollbar). */
static lv_obj_t *aic_box(lv_obj_t *par, lv_coord_t w, lv_coord_t h,
                         lv_color_t col, lv_coord_t radius)
{
    lv_obj_t *o = lv_obj_create(par ? par : s_content);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, col, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* Coloured, left-aligned label; wraps within `w` px when `wrap` is set. */
static lv_obj_t *aic_text(const char *txt, const lv_font_t *font, lv_color_t col,
                          lv_align_t align, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, bool wrap)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, col, LV_PART_MAIN);
    if (wrap) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    lv_obj_align(l, align, x, y);
    return l;
}

/* Top status bar: logo square (left) + STATE title + 4 ascending signal bars. */
static void aic_topbar(const char *title)
{
    lv_obj_t *logo = aic_box(s_content, 14, 14, AC_GREY, 3);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 8, 7);

    lv_obj_t *t = lv_label_create(s_content);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, AIC_F_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(t, AC_GREY, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(t, 2, LV_PART_MAIN);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 30, 8);

    static const lv_coord_t hs[4] = {5, 8, 11, 14};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = aic_box(s_content, 3, hs[i], AC_BARS, 1);
        lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -((3 - i) * 5) - 2, 8 + (14 - hs[i]));
    }
}

/* Bottom status line: small grey left + right labels (either may be NULL). */
static void aic_footer(const char *left, const char *right)
{
    if (left && left[0]) {
        aic_text(left, AIC_F_SMALL, AC_GREY, LV_ALIGN_BOTTOM_LEFT, 8, -6, 0, false);
    }
    if (right && right[0]) {
        aic_text(right, AIC_F_SMALL, AC_GREY, LV_ALIGN_BOTTOM_RIGHT, -8, -6, 0, false);
    }
}

/* A filled action button pinned to a bottom corner (Deny=grey / Allow=orange). */
static void aic_btn(const char *text, lv_color_t bg, bool left)
{
    lv_obj_t *b = aic_box(s_content, 138, 32, bg, 6);
    lv_obj_align(b, left ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT,
                 left ? 10 : -10, -8);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, AIC_F_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, AC_WHITE, LV_PART_MAIN);
    lv_obj_center(l);
}

/* A static arc SEGMENT (transparent bg, no knob, no indicator) — the one curved
 * primitive. Angles use LVGL's convention: 0=3 o'clock, 90=6 (bottom), 270=12
 * (top), increasing clockwise. A segment through 90 curves like a smile (U); a
 * segment through 270 curves like a frown / closed happy-eye (a shallow ∩). */
static void aic_arc(lv_obj_t *par, lv_coord_t d, lv_coord_t width,
                    uint16_t a0, uint16_t a1, lv_color_t col, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *a = lv_arc_create(par);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);              /* no drag knob */
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, d, d);
    lv_arc_set_bg_angles(a, a0, a1);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, col, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 0, LV_PART_INDICATOR);     /* hide value arc */
    lv_obj_align(a, LV_ALIGN_CENTER, x, y);
}

/* Robot mascot, centred at (dx,dy) offset from the panel centre. A rounded white
 * head with a little top nub, capsule eyes, and an arc mouth whose curve encodes
 * the mood: smile (happy), flat bar (neutral), frown (worried/sad). Happy also
 * closes the eyes into two upward arcs. `accent_marks` adds two orange antenna
 * ticks above the head (alert / celebrate). Returns the head (for badge anchor).*/
enum { AIC_FACE_NEUTRAL, AIC_FACE_HAPPY, AIC_FACE_WORRIED, AIC_FACE_SAD };
static lv_obj_t *aic_face(lv_coord_t dx, lv_coord_t dy, int expr, bool accent_marks)
{
    lv_obj_t *head = aic_box(s_content, 66, 60, AC_WHITE, 20);
    lv_obj_align(head, LV_ALIGN_CENTER, dx, dy);

    /* Little top nub (sibling so it isn't clipped): a tab poking out of the top,
     * same white so it merges with the head — the mockup robot's signature. */
    lv_obj_t *nub = aic_box(s_content, 16, 9, AC_WHITE, 4);
    lv_obj_align_to(nub, head, LV_ALIGN_OUT_TOP_MID, 0, 5);

    if (expr == AIC_FACE_HAPPY) {
        /* Closed, happy eyes: two small upward arcs (^ ^). */
        aic_arc(head, 15, 4, 205, 335, AC_EYE, -15, -2);
        aic_arc(head, 15, 4, 205, 335, AC_EYE,  15, -2);
        /* Big smile (U through the bottom). */
        aic_arc(head, 30, 5, 25, 155, AC_EYE, 0, 4);
    } else {
        /* Capsule eyes; sad ones a touch smaller/higher = subdued. */
        lv_coord_t ew = (expr == AIC_FACE_SAD) ? 10 : 12;
        lv_coord_t eh = (expr == AIC_FACE_SAD) ? 14 : 18;
        lv_coord_t ey = (expr == AIC_FACE_SAD) ? -3 : -6;
        lv_obj_t *le = aic_box(head, ew, eh, AC_EYE, ew / 2);
        lv_obj_align(le, LV_ALIGN_CENTER, -15, ey);
        lv_obj_t *re = aic_box(head, ew, eh, AC_EYE, ew / 2);
        lv_obj_align(re, LV_ALIGN_CENTER, 15, ey);

        if (expr == AIC_FACE_NEUTRAL) {
            lv_obj_t *m = aic_box(head, 18, 5, AC_EYE, 2);   /* calm flat mouth */
            lv_obj_align(m, LV_ALIGN_CENTER, 0, 16);
        } else {
            aic_arc(head, 28, 5, 205, 335, AC_EYE, 0, 22);   /* frown (∩) */
        }
    }

    if (accent_marks) {
        lv_obj_t *m1 = aic_box(s_content, 4, 13, AC_ACCENT, 2);
        lv_obj_align_to(m1, head, LV_ALIGN_OUT_TOP_MID, -15, -3);
        lv_obj_t *m2 = aic_box(s_content, 4, 13, AC_ACCENT, 2);
        lv_obj_align_to(m2, head, LV_ALIGN_OUT_TOP_MID, 15, -3);
    }
    return head;
}

/* Left = mascot, right = a headline + a sub-line. The shared body layout used by
 * WORKING / NEED YOU / ATTENTION / DONE. */
static void aic_body_lr(int expr, bool marks, const char *headline,
                        lv_color_t head_col, const char *sub)
{
    aic_face(-96, 4, expr, marks);
    aic_text(headline, AIC_F_BIG, head_col, LV_ALIGN_TOP_LEFT, 116, 42, 196, true);
    aic_text(sub, AIC_F_SMALL, AC_GREY, LV_ALIGN_TOP_LEFT, 116, 100, 196, true);
}

static void aic_draw(const struct aic_screen_model *m)
{
    lv_obj_clean(s_content);

    /* Standby (pre-connect / disconnected). */
    if (m->state[0] == '\0') {
        aic_face(-96, 4, AIC_FACE_NEUTRAL, false);
        aic_text("AI Companion", AIC_F_BIG, AC_WHITE, LV_ALIGN_TOP_LEFT, 116, 44, 196, true);
        aic_text("waiting for host", AIC_F_SMALL, AC_GREY, LV_ALIGN_TOP_LEFT, 116, 100, 196, true);
        return;
    }

    if (strcmp(m->state, "READY") == 0) {                 /* HOME */
        aic_topbar("HOME");
        aic_face(-96, 4, AIC_FACE_NEUTRAL, false);
        aic_text(or_dash(m->clock), AIC_F_HUGE, AC_WHITE, LV_ALIGN_CENTER, 48, -4, 0, false);
        aic_footer("READY WHEN YOU ARE", or_dash(m->clock));
        return;
    }

    if (strcmp(m->state, "TASK") == 0) {                  /* WORKING */
        aic_topbar("WORKING");
        aic_body_lr(AIC_FACE_NEUTRAL, false, or_dash(m->task), AC_WHITE, or_dash(m->line));
        aic_footer("WORKING", "STAYING ON IT");
        return;
    }

    if (strcmp(m->state, "WAITING") == 0) {               /* NEED YOU */
        aic_topbar("NEED YOU");
        aic_body_lr(AIC_FACE_WORRIED, true, "I NEED YOUR HELP", AC_WHITE, or_dash(m->task));
        return;
    }

    if (strcmp(m->state, "PERMISSION") == 0) {            /* PERMISSION detail */
        aic_topbar("PERMISSION");
        aic_text(or_dash(m->question), AIC_F_BIG, AC_WHITE, LV_ALIGN_TOP_LEFT, 12, 34, 296, true);
        aic_text(or_dash(m->task), AIC_F_SMALL, AC_GREY, LV_ALIGN_TOP_LEFT, 12, 90, 296, true);
        aic_btn(LV_SYMBOL_CLOSE " Deny", AC_ROW, true);
        aic_btn(LV_SYMBOL_OK " Allow", AC_ACCENT, false);
        return;
    }

    if (strcmp(m->state, "STOPPED") == 0) {               /* ATTENTION */
        aic_topbar("ATTENTION");
        lv_obj_t *head = aic_face(-96, 4, AIC_FACE_SAD, false);
        lv_obj_t *badge = aic_box(s_content, 22, 22, AC_ACCENT, 11);
        lv_obj_align_to(badge, head, LV_ALIGN_TOP_RIGHT, 8, -6);
        lv_obj_t *bang = lv_label_create(badge);
        lv_label_set_text(bang, "!");
        lv_obj_set_style_text_font(bang, AIC_F_MED, LV_PART_MAIN);
        lv_obj_set_style_text_color(bang, AC_WHITE, LV_PART_MAIN);
        lv_obj_center(bang);
        aic_text(or_dash(m->task), AIC_F_BIG, AC_WHITE, LV_ALIGN_TOP_LEFT, 116, 42, 196, true);
        aic_text(or_dash(m->line), AIC_F_SMALL, AC_GREY, LV_ALIGN_TOP_LEFT, 116, 100, 196, true);
        return;
    }

    if (strcmp(m->state, "DONE") == 0) {                  /* DONE */
        aic_topbar("DONE");
        aic_body_lr(AIC_FACE_HAPPY, true, or_dash(m->task), AC_WHITE, or_dash(m->line));
        aic_footer("DONE", "GREAT WORK!");
        return;
    }

    if (strcmp(m->state, "SESSIONS") == 0) {              /* Session list */
        aic_topbar("SESSIONS");
        lv_obj_t *row = aic_box(s_content, 300, 36, AC_ROW, 6);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 42);
        lv_obj_t *dot = aic_box(row, 10, 10, AC_ACCENT, 5);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 10, 0);
        aic_text(or_dash(m->task), AIC_F_MED, AC_WHITE, LV_ALIGN_TOP_LEFT, 42, 51, 256, false);
        aic_footer(LV_SYMBOL_LIST " your tasks", NULL);
        return;
    }

    if (strcmp(m->state, "LISTENING") == 0) {             /* Voice listening */
        aic_topbar("LISTENING");
        lv_obj_t *circ = aic_box(s_content, 60, 60, AC_ROW, 30);
        lv_obj_align(circ, LV_ALIGN_CENTER, 0, -6);
        lv_obj_t *mic = aic_box(circ, 16, 26, AC_WHITE, 8);
        lv_obj_align(mic, LV_ALIGN_CENTER, 0, -2);
        lv_obj_t *w1 = aic_box(s_content, 4, 26, AC_ACCENT, 2);
        lv_obj_align(w1, LV_ALIGN_CENTER, -46, -6);
        lv_obj_t *w2 = aic_box(s_content, 4, 26, AC_ACCENT, 2);
        lv_obj_align(w2, LV_ALIGN_CENTER, 46, -6);
        aic_text("Speak now...", AIC_F_MED, AC_WHITE, LV_ALIGN_CENTER, 0, 46, 0, false);
        return;
    }

    if (strcmp(m->state, "PROCESSING") == 0) {            /* Voice processing */
        aic_topbar("PROCESSING");
        aic_face(-70, -10, AIC_FACE_NEUTRAL, false);
        for (int i = 0; i < 3; i++) {
            lv_obj_t *d = aic_box(s_content, 8, 8, AC_ACCENT, 4);
            lv_obj_align(d, LV_ALIGN_CENTER, 12 + i * 16, -10);
        }
        aic_text("Understanding your request...", AIC_F_SMALL, AC_WHITE,
                 LV_ALIGN_BOTTOM_MID, 0, -18, 0, false);
        return;
    }

    if (strcmp(m->state, "OPENING") == 0) {               /* Opening result */
        aic_topbar("OPENING");
        lv_obj_t *doc = aic_box(s_content, 46, 54, AC_ROW, 6);
        lv_obj_align(doc, LV_ALIGN_CENTER, 0, -20);
        for (int i = 0; i < 3; i++) {
            lv_obj_t *ln = aic_box(doc, 26, 4, AC_ACCENT, 2);
            lv_obj_align(ln, LV_ALIGN_TOP_LEFT, 10, 12 + i * 12);
        }
        aic_text("Opening result...", AIC_F_MED, AC_WHITE, LV_ALIGN_CENTER, 0, 32, 0, false);
        aic_text(or_dash(m->task), AIC_F_SMALL, AC_GREY, LV_ALIGN_CENTER, 0, 56, 0, false);
        return;
    }

    /* Unknown state (forward-compat): standby-style fallback. */
    aic_topbar(or_dash(m->state));
    aic_body_lr(AIC_FACE_NEUTRAL, false, "AI Companion", AC_WHITE, or_dash(m->state));
}

/* --- boot splash (colour only) --------------------------------------------- */
/*
 * A black-background power-on animation modelled on optimus_boot_preview.html: a
 * static thick white inner ring, a rotating white outer ring, and a tagline. It
 * is a full-screen opaque overlay laid OVER the status screen at init; a one-shot
 * LVGL timer removes it after ~3.2 s, revealing whatever state is current by then
 * (standby if the host is still silent). The outer ring is an lv_spinner (an
 * arc that LVGL rotates on its own animation timer) rather than a rotated 52-tooth
 * bitmap — a spinning arc stays smooth under software rendering at 8 MHz SPI.
 */
static void aic_boot_done(lv_timer_t *t)
{
    lv_obj_t *ov = (lv_obj_t *)t->user_data;
    if (ov != NULL) {
        lv_obj_del(ov);   /* deletes the spinner + its animation with it */
    }
    /* repeat_count was 1, so LVGL deletes this timer itself after we return. */
}

static void aic_boot_splash(lv_obj_t *screen)
{
    lv_obj_t *ov = lv_obj_create(screen);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_color(ov, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    /* Inner static thick ring (a circle outline). */
    lv_obj_t *inner = lv_obj_create(ov);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, 56, 56);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(inner, AC_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(inner, 9, LV_PART_MAIN);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(inner, LV_ALIGN_CENTER, 0, -18);

    /* Outer rotating ring: dim full track (MAIN) + bright sweeping arc (INDICATOR),
     * one turn every 1.4 s. */
    lv_obj_t *sp = lv_spinner_create(ov, 1400, 90);
    lv_obj_set_size(sp, 104, 104);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, -18);
    lv_obj_set_style_arc_color(sp, lv_color_hex(0x2A2E36), LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, AC_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sp, 8, LV_PART_INDICATOR);

    /* Tagline (Latin-only Montserrat, matches the reference wording). */
    lv_obj_t *tag = lv_label_create(ov);
    lv_label_set_text(tag, "Build anything with Optimus");
    lv_obj_set_style_text_font(tag, AIC_F_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag, AC_WHITE, LV_PART_MAIN);
    lv_obj_align(tag, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_timer_t *t = lv_timer_create(aic_boot_done, 3200, ov);
    lv_timer_set_repeat_count(t, 1);
}

#else
/* ======================= MONO 128x64 layout (verbatim) ===================== */

/* Plain white label at an alignment (mono uses only white on black). */
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

#if defined(AIC_COLOR_UI)
    /* Lay the black boot animation over the top; it removes itself after ~3.2 s. */
    aic_boot_splash(screen);
#endif

    return screen;
}
