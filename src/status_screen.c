/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Custom ZMK status screen for the AI Companion device.
 *
 * MONO PROTOTYPE of the "12 Core Screen States" UI on the on-board 128x64
 * SSD1306 OLED, built while the colour 1.9" ST7789 panel ships. It draws
 * text-forward, simplified versions of each state (the mascot art + colour of
 * the mockup arrive later on the colour panel). There is no host link yet
 * (Phase 5), so the 12 states AUTO-CYCLE on a timer to let all screens be
 * reviewed on real hardware. The layout / per-screen content / state list all
 * carry over to the colour build; only the styling is redone there.
 *
 * ZMK wiring (verified against zmkfirmware/zmk v0.3.0):
 *   - CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y makes ZMK call
 *     zmk_display_status_screen() (app/src/display/main.c) instead of the
 *     built-in widget screen. main.c defines a weak stub returning NULL; this
 *     strong definition overrides it (source is compiled into the `app` target
 *     by our module CMakeLists, so no duplicate symbol and the override wins).
 *   - LVGL is v8.3: lv_timer, lv_bar and the LV_SYMBOL_* glyphs are available.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

/* Declared by ZMK in app/include/zmk/display/status_screen.h. Declared locally
 * so this source does not depend on ZMK's PRIVATE app include path. */
lv_obj_t *zmk_display_status_screen(void);

/* Palette. On a 1bpp panel only black/white matter; the mockup is white-on-dark
 * so we draw white on black. If the panel shows up inverted, swap these two. */
#define AIC_FG lv_color_white()
#define AIC_BG lv_color_black()

#define AIC_STATE_COUNT 12
#define AIC_CYCLE_MS 5000

enum aic_state {
    ST_HOME = 0,
    ST_SESSIONS,
    ST_WORKING,
    ST_NEED_YOU,
    ST_PERMISSIONS,
    ST_PERMISSION,
    ST_DONE,
    ST_ATTENTION,
    ST_TIMEOUT,
    ST_LISTENING,
    ST_PROCESSING,
    ST_OPENING,
};

static lv_obj_t *s_content; /* full-screen container we clear + rebuild per state */
static uint8_t s_state;     /* current state index */

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

/* Top bar: STATE NAME (left) + a wifi glyph (right). */
static void aic_header(const char *title)
{
    aic_label(title, &lv_font_montserrat_12, LV_ALIGN_TOP_LEFT, 0, 0);
    aic_label(LV_SYMBOL_WIFI, &lv_font_montserrat_12, LV_ALIGN_TOP_RIGHT, 0, 0);
}

/* Bottom bar: status (left) + optional hint (right). */
static void aic_footer(const char *left, const char *right)
{
    if (left) {
        aic_label(left, &lv_font_montserrat_10, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    if (right) {
        aic_label(right, &lv_font_montserrat_10, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
}

/* "name .............. value" list row at vertical offset y (from top). */
static void aic_row(const char *name, const char *value, lv_coord_t y,
                    const lv_font_t *font)
{
    aic_label(name, font, LV_ALIGN_TOP_LEFT, 0, y);
    aic_label(value, font, LV_ALIGN_TOP_RIGHT, 0, y);
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

/* Horizontal progress bar (used by WORKING). */
static void aic_bar(int value, lv_coord_t y)
{
    lv_obj_t *bar = lv_bar_create(s_content);
    lv_obj_set_size(bar, 120, 8);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, AIC_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, AIC_FG, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, AIC_FG, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
}

/* --- per-state rendering --------------------------------------------------- */

static void aic_render(uint8_t st)
{
    lv_obj_clean(s_content);

    switch (st) {
    case ST_HOME:
        aic_header("HOME");
        aic_row("RUNNING", "2", 16, &lv_font_montserrat_12);
        aic_row("NEED YOU", "1", 29, &lv_font_montserrat_12);
        aic_row("DONE", "8", 42, &lv_font_montserrat_12);
        aic_footer("READY", "10:24");
        break;

    case ST_SESSIONS:
        aic_header("SESSIONS");
        aic_row("Website Rede", "RUN", 15, &lv_font_montserrat_10);
        aic_row("Mobile App", "IDLE", 27, &lv_font_montserrat_10);
        aic_row("Test Suite", "NEED", 39, &lv_font_montserrat_10);
        aic_row("Docs Update", "DONE", 51, &lv_font_montserrat_10);
        break;

    case ST_WORKING:
        aic_header("WORKING");
        aic_label("Website Redesign", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 15);
        aic_label("BUILDING...", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 28);
        aic_label("68%", &lv_font_montserrat_10, LV_ALIGN_TOP_RIGHT, 0, 28);
        aic_bar(68, 40);
        aic_footer("01:24", "STAYING ON IT");
        break;

    case ST_NEED_YOU:
        aic_header("NEED YOU");
        aic_label(LV_SYMBOL_BELL "  !", &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 0, 15);
        aic_label("I NEED YOUR HELP", &lv_font_montserrat_12, LV_ALIGN_TOP_LEFT, 0, 33);
        aic_label("1 DECISION PENDING", &lv_font_montserrat_10, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        break;

    case ST_PERMISSIONS:
        aic_header("PERMS");
        aic_row("Run tests?", "2m", 16, &lv_font_montserrat_12);
        aic_row("Deploy stg?", "8m", 30, &lv_font_montserrat_12);
        aic_row("Access db?", "12m", 44, &lv_font_montserrat_12);
        break;

    case ST_PERMISSION:
        aic_header("PERMISSION");
        aic_label("Run tests?", &lv_font_montserrat_12, LV_ALIGN_TOP_LEFT, 0, 15);
        aic_label("248 tests, ~3 min", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 31);
        aic_chip(LV_SYMBOL_CLOSE " Deny", LV_ALIGN_BOTTOM_LEFT);
        aic_chip(LV_SYMBOL_OK " Allow", LV_ALIGN_BOTTOM_RIGHT);
        break;

    case ST_DONE:
        aic_header("DONE");
        aic_label(LV_SYMBOL_OK " 12 tests passed", &lv_font_montserrat_12, LV_ALIGN_TOP_LEFT, 0, 17);
        aic_label("All checks successful", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 34);
        aic_footer("00:58", "GREAT WORK!");
        break;

    case ST_ATTENTION:
        aic_header("ATTENTION");
        aic_label(LV_SYMBOL_WARNING " Build failed", &lv_font_montserrat_12, LV_ALIGN_TOP_LEFT, 0, 16);
        aic_label("Type error in", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 34);
        aic_label("Button.tsx", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 46);
        break;

    case ST_TIMEOUT:
        aic_header("TIMEOUT");
        aic_label("Still working...", &lv_font_montserrat_12, LV_ALIGN_TOP_LEFT, 0, 16);
        aic_label("Running for 20 min", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 34);
        aic_footer("20:00", "KEEP GOING?");
        break;

    case ST_LISTENING:
        aic_header("LISTENING");
        aic_label(LV_SYMBOL_AUDIO, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 0, 18);
        aic_label("Speak now...", &lv_font_montserrat_12, LV_ALIGN_TOP_MID, 0, 42);
        break;

    case ST_PROCESSING:
        aic_header("PROCESSING");
        aic_label(".  .  .", &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 0, 18);
        aic_label("Understanding", &lv_font_montserrat_10, LV_ALIGN_TOP_MID, 0, 38);
        aic_label("your request...", &lv_font_montserrat_10, LV_ALIGN_TOP_MID, 0, 50);
        break;

    case ST_OPENING:
        aic_header("OPENING");
        aic_label(LV_SYMBOL_FILE " Opening result...", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 20);
        aic_label("Website Redesign", &lv_font_montserrat_10, LV_ALIGN_TOP_LEFT, 0, 38);
        break;

    default:
        break;
    }
}

/* --- demo cycling ---------------------------------------------------------- */

static void aic_tick(lv_timer_t *timer)
{
    ARG_UNUSED(timer);
    s_state = (s_state + 1) % AIC_STATE_COUNT;
    aic_render(s_state);
}

/* --- entry point ----------------------------------------------------------- */

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

    s_state = 0;
    aic_render(s_state);

    /* Auto-advance through the 12 states (no host link yet). */
    lv_timer_create(aic_tick, AIC_CYCLE_MS, NULL);

    return screen;
}
