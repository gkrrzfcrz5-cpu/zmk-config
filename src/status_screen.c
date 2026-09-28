/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * DIAGNOSTIC MINIMAL custom status screen.
 *
 * Mirrors ZMK's known-good built-in status_screen.c EXACTLY: create the screen
 * with lv_obj_create(NULL) and drop plain labels straight on it, no wrapper
 * container, no explicit colours/styles (let the theme drive it), no timer.
 * Purpose: isolate whether the "全是亂碼" garbage came from our full-size
 * container + custom styles, or from a lower layer (buffer/theme). If this
 * renders cleanly, the fault was in the rich layout; if it is still garbage,
 * the fault is in the 1bpp pipeline/theme for custom screens.
 */

#include <lvgl.h>

lv_obj_t *zmk_display_status_screen(void);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *l1 = lv_label_create(screen);
    lv_label_set_text(l1, "AI COMPANION");
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *l2 = lv_label_create(screen);
    lv_label_set_text(l2, "mono UI test");
    lv_obj_align(l2, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *l3 = lv_label_create(screen);
    lv_label_set_text(l3, "line 3 - 12345");
    lv_obj_align(l3, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return screen;
}
