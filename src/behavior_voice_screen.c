/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Custom ZMK behavior: drive the device's LOCAL "listening" screen while the
 * Voice button is physically held (push-to-talk visual feedback).
 *
 * The Voice key is bound to the `voice_ptt` macro (colour keymap). That macro
 * taps Cmd+M to Optimus, then wraps THIS behavior in &macro_press /
 * &macro_release around &macro_pause_for_release, so binding_pressed fires on
 * the physical press and binding_released fires on the physical release — the
 * whole hold window. We flip the local LISTENING screen on/off to match,
 * independent of any host (src/aic_comm.c marshals the redraw onto the display
 * work queue, since LVGL is single-threaded).
 *
 * Modelled on src/behavior_input.c. No hardware, no init dependency, no
 * parameter — default kernel init priority, #binding-cells = <0>.
 */

#define DT_DRV_COMPAT aic_behavior_voice_screen

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "aic_input.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    aic_comm_voice_screen(true);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    aic_comm_voice_screen(false);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_voice_screen_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api behavior_voice_screen_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define AIC_VOICE_SCREEN_INST(n)                                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_voice_screen_init, NULL, NULL, NULL,                        \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                        \
                            &behavior_voice_screen_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AIC_VOICE_SCREEN_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
