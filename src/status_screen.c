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
#include <stdio.h>
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

/* A short straight stroke (polyline). `pts` MUST have static storage — lv_line
 * keeps the pointer, it does not copy. Returns the line so the caller can align
 * it. Used for the mascot's worried eyebrows, celebrate sparks, and alert burst
 * (angled marks that a rectangle can't make without rotation). */
static lv_obj_t *aic_line(lv_obj_t *par, const lv_point_t *pts, uint16_t n,
                          lv_color_t col, lv_coord_t width)
{
    lv_obj_t *l = lv_line_create(par ? par : s_content);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_color(l, col, LV_PART_MAIN);
    lv_obj_set_style_line_width(l, width, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(l, true, LV_PART_MAIN);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

/* Static stroke geometry for the mascot accents (lv_line keeps the pointer). */
static const lv_point_t BROW_L[]  = {{0, 4}, {10, 0}};   /* \_ worried: inner (right) end higher */
static const lv_point_t BROW_R[]  = {{0, 0}, {10, 4}};   /* _/ worried: inner (left) end higher */
static const lv_point_t SPARK_L[] = {{9, 10}, {0, 0}};   /* celebrate: short stroke splaying up-left  */
static const lv_point_t SPARK_R[] = {{0, 10}, {9, 0}};   /* celebrate: short stroke splaying up-right */

/* Big alert "explosion": rays radiating out from a point above the head. Drawn in
 * a 64x44 container's own coordinates from a common centre near (32,30), so the
 * whole starburst places with one align. lv_line keeps the pointer -> static. */
static const lv_point_t BURST_U[]  = {{32, 24}, {32, 2}};    /* straight up            */
static const lv_point_t BURST_UL[] = {{27, 24}, {10, 8}};    /* up and to the left     */
static const lv_point_t BURST_UR[] = {{37, 24}, {54, 8}};    /* up and to the right    */
static const lv_point_t BURST_L[]  = {{26, 28}, {4, 22}};    /* out to the left        */
static const lv_point_t BURST_R[]  = {{38, 28}, {60, 22}};   /* out to the right       */

/* Big orange "explosion" starburst, floated above `head`: five splayed rays in a
 * 64x44 transparent container (a child of s_content, so lv_obj_clean removes it).
 * Emphasised over the old tiny in-face rays so NEED YOU reads as "help!" at a
 * glance even with a smaller mascot. */
static void aic_burst(lv_obj_t *head)
{
    lv_obj_t *box = lv_obj_create(s_content);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 64, 44);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    aic_line(box, BURST_U,  2, AC_ACCENT, 4);
    aic_line(box, BURST_UL, 2, AC_ACCENT, 4);
    aic_line(box, BURST_UR, 2, AC_ACCENT, 4);
    aic_line(box, BURST_L,  2, AC_ACCENT, 4);
    aic_line(box, BURST_R,  2, AC_ACCENT, 4);
    lv_obj_align_to(box, head, LV_ALIGN_OUT_TOP_MID, 0, 6);
}

/* Robot mascot, centred at (dx,dy) offset from the panel centre, sized to `scale`
 * percent (100 = full). A rounded white head with a little top nub and two small
 * symmetric eyes. Mood is drawn to match the 12-screen mockup: neutral = a short
 * dash mouth; happy = closed smiling arc eyes + a U smile; worried/sad = a frown +
 * two angled "pleading" eyebrows. `accent_marks` adds orange marks above the head:
 * two angled corner sparks when happy (celebrate), or the big alert burst otherwise.
 * Returns the head (for badge/burst anchor). */
enum { AIC_FACE_NEUTRAL, AIC_FACE_HAPPY, AIC_FACE_WORRIED, AIC_FACE_SAD };
static lv_obj_t *aic_face(lv_coord_t dx, lv_coord_t dy, int expr, bool accent_marks,
                          int scale)
{
#define SP(v) ((lv_coord_t)((v) * scale / 100))
    lv_obj_t *head = aic_box(s_content, SP(62), SP(60), AC_WHITE, SP(18));
    lv_obj_align(head, LV_ALIGN_CENTER, dx, dy);

    /* Little top tab (sibling so it isn't clipped): a rounded white pill floating
     * just ABOVE the head with a small black gap, so it reads as a separate mark —
     * the mockup robot's signature. (Was +5, which pushed it down into the head so
     * the white tab merged into the white head and vanished.) */
    lv_obj_t *nub = aic_box(s_content, SP(16), SP(9), AC_WHITE, 4);
    lv_obj_align_to(nub, head, LV_ALIGN_OUT_TOP_MID, 0, -4);

    if (expr == AIC_FACE_HAPPY) {
        /* Closed smiling eyes (two shallow arcs) + a U smile. */
        aic_arc(head, SP(16), 4, 205, 335, AC_EYE, SP(-13), SP(-3));
        aic_arc(head, SP(16), 4, 205, 335, AC_EYE, SP(13),  SP(-3));
        aic_arc(head, SP(26), 5, 25, 155, AC_EYE, 0, SP(6));
    } else {
        /* Small symmetric eyes (short rounded bars) — same on every non-happy face. */
        lv_obj_t *le = aic_box(head, SP(10), SP(13), AC_EYE, 5);
        lv_obj_align(le, LV_ALIGN_CENTER, SP(-13), SP(-4));
        lv_obj_t *re = aic_box(head, SP(10), SP(13), AC_EYE, 5);
        lv_obj_align(re, LV_ALIGN_CENTER, SP(13), SP(-4));

        if (expr == AIC_FACE_NEUTRAL) {
            lv_obj_t *m = aic_box(head, SP(10), SP(4), AC_EYE, 2);  /* short dash mouth */
            lv_obj_align(m, LV_ALIGN_CENTER, 0, SP(15));
        } else {
            /* Worried/sad: a frown + two angled "pleading" eyebrows over the eyes. */
            aic_arc(head, SP(24), 5, 205, 335, AC_EYE, 0, SP(20));
            lv_obj_t *bl = aic_line(head, BROW_L, 2, AC_EYE, 3);
            lv_obj_align(bl, LV_ALIGN_CENTER, SP(-13), SP(-17));
            lv_obj_t *br = aic_line(head, BROW_R, 2, AC_EYE, 3);
            lv_obj_align(br, LV_ALIGN_CENTER, SP(13), SP(-17));
        }
    }

    if (accent_marks) {
        if (expr == AIC_FACE_HAPPY) {
            /* Two angled orange sparks splaying up-and-out from the top corners
             * (celebrate confetti, not horns). */
            lv_obj_t *sl = aic_line(s_content, SPARK_L, 2, AC_ACCENT, 3);
            lv_obj_align_to(sl, head, LV_ALIGN_OUT_TOP_LEFT, 2, 2);
            lv_obj_t *sr = aic_line(s_content, SPARK_R, 2, AC_ACCENT, 3);
            lv_obj_align_to(sr, head, LV_ALIGN_OUT_TOP_RIGHT, -11, 2);
        } else {
            aic_burst(head);
        }
    }
    return head;
#undef SP
}

/* Left = mascot, right = a headline + a sub-line. The shared body layout used by
 * WORKING / NEED YOU / ATTENTION / DONE. */
static void aic_body_lr(int expr, bool marks, const char *headline,
                        lv_color_t head_col, const char *sub)
{
    aic_face(-96, 4, expr, marks, 100);
    aic_text(headline, AIC_F_BIG, head_col, LV_ALIGN_TOP_LEFT, 116, 42, 196, true);
    aic_text(sub, AIC_F_SMALL, AC_GREY, LV_ALIGN_TOP_LEFT, 116, 100, 196, true);
}

static void aic_draw(const struct aic_screen_model *m)
{
    lv_obj_clean(s_content);

    /* Standby (pre-connect / disconnected). */
    if (m->state[0] == '\0') {
        aic_face(-96, 4, AIC_FACE_NEUTRAL, false, 100);
        aic_text("AI Companion", AIC_F_BIG, AC_WHITE, LV_ALIGN_TOP_LEFT, 116, 44, 196, true);
        aic_text("waiting for host", AIC_F_SMALL, AC_GREY, LV_ALIGN_TOP_LEFT, 116, 100, 196, true);
        return;
    }

    if (strcmp(m->state, "READY") == 0) {                 /* HOME */
        aic_topbar("HOME");
        aic_face(-96, 4, AIC_FACE_NEUTRAL, false, 100);

        /* Big clock with the HOUR digits in orange (accent), minutes in white.
         * LVGL recolor markup: "#RRGGBB text#" — the space after the hex is the
         * separator (not printed). Split "HH:MM" on the colon; if there's no
         * colon (e.g. the "—" placeholder) just show it plain white. */
        lv_obj_t *clk = lv_label_create(s_content);
        lv_label_set_recolor(clk, true);
        const char *cl = or_dash(m->clock);
        const char *colon = strchr(cl, ':');
        if (colon != NULL) {
            char buf[24];
            snprintf(buf, sizeof(buf), "#F5551E %.*s#%s", (int)(colon - cl), cl, colon);
            lv_label_set_text(clk, buf);
        } else {
            lv_label_set_text(clk, cl);
        }
        lv_obj_set_style_text_font(clk, AIC_F_HUGE, LV_PART_MAIN);
        lv_obj_set_style_text_color(clk, AC_WHITE, LV_PART_MAIN);
        lv_obj_align(clk, LV_ALIGN_CENTER, 48, -4);

        aic_footer("READY WHEN YOU ARE", NULL);
        return;
    }

    if (strcmp(m->state, "TASK") == 0) {                  /* WORKING */
        aic_topbar("WORKING");
        aic_body_lr(AIC_FACE_NEUTRAL, false, or_dash(m->task), AC_WHITE, or_dash(m->line));
        aic_footer("IN PROGRESS", "STAYING ON IT");
        return;
    }

    if (strcmp(m->state, "WAITING") == 0) {               /* NEED YOU */
        aic_topbar("NEED YOU");
        /* Smaller worried mascot + a big separate orange explosion above it, so the
         * "help!" burst is the focal point (user: keep/emphasise the explosion,
         * shrink the face). Custom layout instead of aic_body_lr. */
        lv_obj_t *head = aic_face(-96, 12, AIC_FACE_WORRIED, false, 72);
        aic_burst(head);
        aic_text("I NEED YOUR HELP", AIC_F_BIG, AC_WHITE, LV_ALIGN_TOP_LEFT, 116, 42, 196, true);
        aic_text(or_dash(m->task), AIC_F_SMALL, AC_GREY, LV_ALIGN_TOP_LEFT, 116, 100, 196, true);
        return;
    }

    if (strcmp(m->state, "PERMISSION") == 0) {            /* PERMISSION detail */
        aic_topbar("PERMISSION");
        aic_text(or_dash(m->question), AIC_F_BIG, AC_WHITE, LV_ALIGN_TOP_LEFT, 12, 34, 296, true);
        aic_text(or_dash(m->task), AIC_F_SMALL, AC_GREY, LV_ALIGN_TOP_LEFT, 12, 90, 296, true);
        aic_btn(LV_SYMBOL_CLOSE " Deny", lv_color_hex(0x3A3F47), true);
        aic_btn(LV_SYMBOL_OK " Allow", AC_ACCENT, false);
        return;
    }

    if (strcmp(m->state, "STOPPED") == 0) {               /* ATTENTION */
        aic_topbar("ATTENTION");
        /* Full-screen red border frame so "something's wrong" reads at a glance.
         * A child of s_content (transparent fill, ~3px red edge) so the next
         * lv_obj_clean() removes it with the rest of the screen. */
        lv_obj_t *frame = lv_obj_create(s_content);
        lv_obj_remove_style_all(frame);
        lv_obj_set_size(frame, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_border_color(frame, lv_color_hex(0xE5342A), LV_PART_MAIN);
        lv_obj_set_style_border_width(frame, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(frame, 8, LV_PART_MAIN);
        lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(frame);
        lv_obj_t *head = aic_face(-96, 4, AIC_FACE_SAD, false, 100);
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
        /* A short list: row 0 = the live task (orange "active" dot), plus a few
         * representative rows (grey "idle" dots) so the screen reads as a list, not
         * one lonely row. Real multi-session data is Phase B; these are sample rows,
         * like the rest of the prototype demo. */
        static const char *SESS_SUB[3] = {
            "deploy pipeline", "doc review", "test sweep",
        };
        for (int i = 0; i < 4; i++) {
            lv_obj_t *row = aic_box(s_content, 300, 30, AC_ROW, 6);
            lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 34 + i * 34);
            lv_obj_t *dot = aic_box(row, 10, 10, i == 0 ? AC_ACCENT : AC_GREY, 5);
            lv_obj_align(dot, LV_ALIGN_LEFT_MID, 10, 0);
            lv_obj_t *l = lv_label_create(row);
            lv_label_set_text(l, i == 0 ? or_dash(m->task) : SESS_SUB[i - 1]);
            lv_obj_set_style_text_font(l, AIC_F_MED, LV_PART_MAIN);
            lv_obj_set_style_text_color(l, i == 0 ? AC_WHITE : AC_GREY, LV_PART_MAIN);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 32, 0);
        }
        aic_footer(LV_SYMBOL_LIST " your tasks", NULL);
        return;
    }

    if (strcmp(m->state, "LISTENING") == 0) {             /* Voice listening */
        aic_topbar("LISTENING");
        lv_obj_t *circ = aic_box(s_content, 60, 60, AC_ROW, 30);
        lv_obj_align(circ, LV_ALIGN_CENTER, 0, -6);
        lv_obj_t *mic = aic_box(circ, 16, 26, AC_WHITE, 8);
        lv_obj_align(mic, LV_ALIGN_CENTER, 0, -2);
        /* A little equalizer: bars of varying height flanking the mic circle, so it
         * reads as "hearing sound" rather than two lonely sticks. */
        static const lv_coord_t EQ_H[6] = {14, 30, 20, 20, 30, 14};
        static const lv_coord_t EQ_X[6] = {-64, -52, -40, 40, 52, 64};
        for (int i = 0; i < 6; i++) {
            lv_obj_t *bar = aic_box(s_content, 4, EQ_H[i], AC_ACCENT, 2);
            lv_obj_align(bar, LV_ALIGN_CENTER, EQ_X[i], -6);
        }
        aic_text("Speak now...", AIC_F_MED, AC_WHITE, LV_ALIGN_CENTER, 0, 46, 0, false);
        return;
    }

    if (strcmp(m->state, "PROCESSING") == 0) {            /* Voice processing */
        aic_topbar("PROCESSING");
        /* Face + 3 dots centred as a group: face left of centre, dots just to its
         * right, so the pair sits in the middle instead of hugging the left edge. */
        aic_face(-34, -6, AIC_FACE_NEUTRAL, false, 100);
        for (int i = 0; i < 3; i++) {
            lv_obj_t *d = aic_box(s_content, 8, 8, AC_ACCENT, 4);
            lv_obj_align(d, LV_ALIGN_CENTER, 34 + i * 16, -6);
        }
        aic_text("Understanding your request...", AIC_F_SMALL, AC_WHITE,
                 LV_ALIGN_BOTTOM_MID, 0, -18, 0, false);
        return;
    }

    if (strcmp(m->state, "OPENING") == 0) {               /* Opening result */
        aic_topbar("OPENING");
        lv_obj_t *doc = aic_box(s_content, 54, 64, AC_ROW, 6);
        lv_obj_align(doc, LV_ALIGN_CENTER, 0, -22);
        for (int i = 0; i < 3; i++) {
            lv_obj_t *ln = aic_box(doc, 32, 4, AC_ACCENT, 2);
            lv_obj_align(ln, LV_ALIGN_TOP_LEFT, 11, 14 + i * 14);
        }
        aic_text("Opening result...", AIC_F_MED, AC_WHITE, LV_ALIGN_CENTER, 0, 34, 0, false);
        aic_text(or_dash(m->task), AIC_F_SMALL, AC_GREY, LV_ALIGN_CENTER, 0, 58, 0, false);
        return;
    }

    /* Unknown state (forward-compat): standby-style fallback. */
    aic_topbar(or_dash(m->state));
    aic_body_lr(AIC_FACE_NEUTRAL, false, "AI Companion", AC_WHITE, or_dash(m->state));
}

/* --- prototype demo auto-cycle -------------------------------------------- */
/*
 * With no host connected there is nothing to show but the standby face, so walk
 * every screen state on a timer — a rolling showcase of the whole UI. Sample
 * data per frame is representative, not real. The first real host message stops
 * the cycle (see aic_screen_render), so this never fights live data.
 */
static lv_timer_t *s_demo_timer;

static const struct aic_screen_model AIC_DEMO[] = {
    { .state = "",           .clock = "" },                 /* standby / waiting */
    { .state = "READY",      .clock = "14:32" },            /* HOME */
    { .state = "TASK",       .task = "Refactoring auth module", .line = "editing session.py" },
    { .state = "WAITING",    .task = "Approve deploy to staging?" },
    { .state = "PERMISSION", .question = "Run database migration?", .task = "migrate 0042_add_index" },
    { .state = "STOPPED",    .task = "Build failed", .line = "3 tests red" },
    { .state = "DONE",       .task = "Shipped 12 files", .line = "all tests green" },
    { .state = "SESSIONS",   .task = "auth refactor" },
    { .state = "LISTENING" },
    { .state = "PROCESSING" },
    { .state = "OPENING",    .task = "report.pdf" },
};
#define AIC_DEMO_N ((int)(sizeof(AIC_DEMO) / sizeof(AIC_DEMO[0])))

static void aic_demo_tick(lv_timer_t *t)
{
    static int i;
    s_model = AIC_DEMO[i];
    aic_draw(&s_model);
    i = (i + 1) % AIC_DEMO_N;
}

/* --- boot splash (colour only) --------------------------------------------- */
/*
 * A black-background power-on animation modelled on the optimus_boot reference: a
 * small white inner ring ("O"), a fine ring of 24 white radial ticks (lv_meter)
 * that slowly rotates, and a tagline. It is a full-screen opaque overlay laid OVER
 * the status screen at init; a one-shot LVGL timer removes it after ~3.2 s,
 * revealing whatever state is current by then (standby if the host is still
 * silent). The tick ring is an lv_meter scale whose rotation is animated — a
 * uniform tick ring reads as a gentle shimmer (matching the reference), and only
 * the meter's bounding box invalidates, so it stays affordable at 8 MHz SPI.
 */

/* Kept for the rotation animation callback (single boot overlay at a time). */
static lv_meter_scale_t *s_boot_scale;

static void aic_boot_spin(void *meter, int32_t rotation)
{
    /* Re-place the ticks at a new start angle; full 360deg range, 24 ticks. */
    lv_meter_set_scale_range((lv_obj_t *)meter, s_boot_scale, 0, 100, 360, rotation);
}

static void aic_boot_done(lv_timer_t *t)
{
    lv_obj_t *ov = (lv_obj_t *)t->user_data;
    if (ov != NULL) {
        lv_obj_del(ov);   /* deletes the meter + its rotation animation with it */
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

    /* Bold white inner ring — a thin "O" (transparent centre, white border). Kept
     * small so it reads as a logo mark, not a giant eyeball (was 88px/20px border,
     * which filled the 170px-wide panel). */
    lv_obj_t *inner = lv_obj_create(ov);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, 46, 46);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(inner, AC_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(inner, 8, LV_PART_MAIN);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(inner, LV_ALIGN_CENTER, 0, -10);

    /* Outer tick ring: 24 thin white radial ticks, slowly rotating. Halved from 48
     * ticks and shrunk from 140->96px so each animation frame redraws ~1/4 the work
     * — the 8 MHz software-SPI redraw of the full meter every frame was what made
     * the rotation stutter. */
    lv_obj_t *meter = lv_meter_create(ov);
    lv_obj_set_size(meter, 96, 96);
    lv_obj_align(meter, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(meter, 0, LV_PART_MAIN);
    lv_obj_clear_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    s_boot_scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, s_boot_scale, 24, 2, 8, AC_WHITE);
    lv_meter_set_scale_range(meter, s_boot_scale, 0, 100, 360, 0);

    /* Gentle continuous rotation (9 s / turn — slower so any dropped frame is less
     * jarring); the 3.2 s splash shows part of one turn. */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, meter);
    lv_anim_set_exec_cb(&a, aic_boot_spin);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, 9000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

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
#if defined(AIC_COLOR_UI)
    /* A real host message wins: stop the prototype auto-cycle for good. */
    if (s_demo_timer != NULL && model->state[0] != '\0') {
        lv_timer_del(s_demo_timer);
        s_demo_timer = NULL;
    }
#endif
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

    /* Prototype showcase: once nothing else is driving the panel, roll through
     * every screen state on a timer (first tick after the boot splash clears).
     * Stopped the moment a real host screen arrives (see aic_screen_render). */
    s_demo_timer = lv_timer_create(aic_demo_tick, 3500, NULL);
#endif

    return screen;
}
