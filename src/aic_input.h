/*
 * Copyright (c) 2026 The AI Companion Contributors
 * SPDX-License-Identifier: MIT
 *
 * Shared contract between the &aic_input keymap behavior
 * (src/behavior_input.c) and the host-communication module (src/aic_comm.c):
 * the physical-button ids and the send entry point.
 */

#pragma once

#include <stdint.h>

/*
 * Physical button ids, passed as the single &aic_input behavior parameter in
 * the keymap. KEEP IN SYNC with the AIC_* macros in
 * boards/shields/ai_companion/ai_companion.keymap.
 */
#define AIC_KEY_VOICE 0
#define AIC_KEY_YES   1
#define AIC_KEY_NO    2
#define AIC_KEY_OPEN  3

/*
 * Report a physical button press to the host: sends an `input` message naming
 * the button (AIC_KEY_*) plus the current screen state + id, over the USB
 * CDC-ACM channel. The host decides what the press means for that screen
 * (docs/phase5-interface-contract.md §5.1). No-op if the channel is not up.
 * Safe to call from a behavior (thread) context; NOT from an ISR.
 */
void aic_comm_send_input(uint32_t key);
