/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Public entry point to the DRV2605L haptic driver (src/behavior_haptic.c), so
 * the host-communication module (src/aic_comm.c) can fire a buzz on a `haptic`
 * message — not just the keymap `&haptic` behavior.
 */

#pragma once

#include <stddef.h>
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
 * A "wait" entry for a waveform sequence: encode a pause between effects so a
 * pattern reads as a distinct rhythm (e.g. click-pause-click = a clean double
 * tap). The DRV2605L treats a sequence byte with bit7 set as a delay of
 * (value & 0x7F) x 10 ms. Range ~10..1270 ms; values are rounded down to 10 ms.
 */
#define AIC_HAPTIC_DELAY_MS(ms) ((uint8_t)(0x80u | (((ms) / 10) & 0x7Fu)))

/*
 * Fire one DRV2605L ROM effect (ERM, internal-trigger). No-op if the haptic
 * behavior/device is not present or its I2C bus is not ready. Safe to call from
 * a thread/work context (it does blocking I2C register writes); NOT from an ISR.
 */
void aic_haptic_play_effect(uint8_t effect_id);

/*
 * Play a sequence of up to 8 waveform-slot entries back-to-back (effect ids
 * and/or AIC_HAPTIC_DELAY_MS() pauses), letting a cue be a designed rhythm
 * rather than a single tap. Entries beyond 8 are ignored. len==0 is a no-op.
 * Same threading rules as aic_haptic_play_effect (blocking I2C, not from ISR).
 */
void aic_haptic_play_seq(const uint8_t *seq, size_t len);
