/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Public entry point to the DRV2605L haptic driver (src/behavior_haptic.c), so
 * the host-communication module (src/aic_comm.c) can fire a buzz on a `haptic`
 * message — not just the keymap `&haptic` behavior.
 */

#pragma once

#include <stdint.h>

/*
 * DRV2605L ROM waveform effect ids (subset), used to give each semantic haptic
 * cue its own feel. Only AIC_HAPTIC_BUZZ1 is HW-verified so far (Phase 4); the
 * distinct feel of the other two is tuned + verified on the real motor in a
 * later step. Values are the effect numbers from the DRV2605L datasheet library.
 */
#define AIC_HAPTIC_STRONG_CLICK 1   /* "Strong Click - 100%" */
#define AIC_HAPTIC_DOUBLE_CLICK 10  /* "Double Click - 100%" */
#define AIC_HAPTIC_SHARP_TICK   24  /* "Sharp Tick 1 - 100%" */
#define AIC_HAPTIC_BUZZ1        47  /* "Buzz 1 - 100%" (HW-verified) */

/*
 * Fire one DRV2605L ROM effect (ERM, internal-trigger). No-op if the haptic
 * behavior/device is not present or its I2C bus is not ready. Safe to call from
 * a thread/work context (it does blocking I2C register writes); NOT from an ISR.
 */
void aic_haptic_play_effect(uint8_t effect_id);
