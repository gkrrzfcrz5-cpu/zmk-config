/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Custom ZMK behavior: report a physical-button press to the host.
 *
 * The 4 buttons (Voice / Yes / No / Open) no longer type placeholder keycodes.
 * Each is bound to `&aic_input <KEY>` in the keymap; on press this behavior
 * calls aic_comm_send_input(), which emits an `input` JSON line over the USB
 * CDC-ACM channel naming the button plus the current screen state + id. The
 * host (Optimus) decides what the press means for the screen shown at that
 * moment — device stays a thin client (docs/phase5-interface-contract.md §5.1).
 *
 * Modelled on src/behavior_haptic.c. This behavior has no hardware and no init
 * dependency, so it uses the default kernel init priority. The single parameter
 * (the AIC_KEY_* id) arrives in binding->param1.
 */

#define DT_DRV_COMPAT aic_behavior_input

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include "aic_input.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    aic_comm_send_input(binding->param1);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    /* Press-only semantics; nothing to do on release. */
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_input_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api behavior_input_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define AIC_INPUT_INST(n)                                                                           \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_input_init, NULL, NULL, NULL,                               \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                       \
                            &behavior_input_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AIC_INPUT_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
