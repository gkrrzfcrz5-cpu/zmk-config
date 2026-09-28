/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Custom ZMK behavior: fire a DRV2605L one-shot haptic effect over raw I2C.
 *
 * There is no DRV2605 driver in Zephyr v3.5 (zmk fork) — verified: no
 * drivers/haptics, no ti,drv2605 binding — so we do raw register writes via the
 * Zephyr I2C API. "Just vibrate": on each key press we run the full DRV2605L
 * buzz sequence (exit standby -> ERM -> library -> waveform -> GO). Doing the
 * whole sequence per press (rather than a one-time init) means our init() does
 * nothing, so init order never matters functionally. The node is still an I2C
 * device in the devicetree, so Zephyr's build-time check_init_priorities
 * requires it to init AFTER the I2C controller: we use
 * CONFIG_APPLICATION_INIT_PRIORITY (90), well after the nRF TWIM controller.
 *
 * Register values verified against the Adafruit_DRV2605 library (matches chip);
 * effect id 47 ("Buzz 1 - 100%") verified from the DRV2605L learn-guide PDF.
 * The device address / bus come from the devicetree node (aic,behavior-haptic
 * @ 0x5a on &xiao_i2c). See docs/haptic-plan.md.
 */

#define DT_DRV_COMPAT aic_behavior_haptic

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* DRV2605L register map (subset). */
#define DRV_REG_MODE     0x01
#define DRV_REG_LIBRARY  0x03
#define DRV_REG_WAVESEQ1 0x04
#define DRV_REG_WAVESEQ2 0x05
#define DRV_REG_GO       0x0C
#define DRV_REG_FEEDBACK 0x1A

#define DRV_MODE_INTTRIG  0x00 /* exit standby, internal trigger mode */
#define DRV_LIBRARY_ERM_A 0x01 /* ERM waveform library A */
#define DRV_EFFECT_BUZZ1  0x2F /* effect 47: "Buzz 1 - 100%" */
#define DRV_WAVESEQ_END   0x00
#define DRV_GO_PLAY       0x01
#define DRV_FEEDBACK_ERM_MASK 0x7F /* clear bit7 (N_ERM_LRA) -> ERM */

struct behavior_haptic_config {
    struct i2c_dt_spec i2c;
};

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_haptic_config *cfg = dev->config;
    uint8_t feedback;

    if (!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("DRV2605L I2C bus not ready");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    /* Exit standby into internal-trigger mode. */
    i2c_reg_write_byte_dt(&cfg->i2c, DRV_REG_MODE, DRV_MODE_INTTRIG);

    /* Select ERM (clear N_ERM_LRA bit7) preserving the other feedback bits. */
    if (i2c_reg_read_byte_dt(&cfg->i2c, DRV_REG_FEEDBACK, &feedback) == 0) {
        i2c_reg_write_byte_dt(&cfg->i2c, DRV_REG_FEEDBACK,
                              feedback & DRV_FEEDBACK_ERM_MASK);
    }

    /* Load an ERM buzz waveform and fire it. */
    i2c_reg_write_byte_dt(&cfg->i2c, DRV_REG_LIBRARY, DRV_LIBRARY_ERM_A);
    i2c_reg_write_byte_dt(&cfg->i2c, DRV_REG_WAVESEQ1, DRV_EFFECT_BUZZ1);
    i2c_reg_write_byte_dt(&cfg->i2c, DRV_REG_WAVESEQ2, DRV_WAVESEQ_END);
    i2c_reg_write_byte_dt(&cfg->i2c, DRV_REG_GO, DRV_GO_PLAY);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_haptic_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api behavior_haptic_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define HAPTIC_INST(n)                                                                              \
    static const struct behavior_haptic_config behavior_haptic_config_##n = {                       \
        .i2c = I2C_DT_SPEC_INST_GET(n)};                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_haptic_init, NULL, NULL, &behavior_haptic_config_##n,       \
                            POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY,                          \
                            &behavior_haptic_driver_api);

DT_INST_FOREACH_STATUS_OKAY(HAPTIC_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
