# Firmware Architecture & Communication Design

> This document captures the *intended* architecture. Sections beyond Phase 2
> are design intent and are marked accordingly; they are not implemented yet.

## System overview

```
+---------------------+        BLE         +--------------------------+
|  AI Companion (MCU)  | <================> |  MacBook (host app)       |
|  XIAO nRF52840 / ZMK |    HID today,     |  runs the AI agents       |
|                      |  custom GATT next  |  manages AI sessions      |
+----------+-----------+                    +-------------+------------+
           |                                               |
   buttons / encoder / OLED / haptic              shows/permits AI actions
```

- The **MacBook** runs the AI models and manages sessions. The **device never
  runs AI models** — it is a physical control + notification surface.
- **Phase 2–3:** device acts as a standard **BLE HID keyboard** (ZMK default).
  Physical controls emit test HID keycodes so each input can be verified on the
  Mac with no custom host software.
- **Phase 5 (design intent):** add a **bidirectional AI-event channel** on top
  of BLE, reusing ZMK's existing BLE stack rather than rewriting it.

## Phase mapping

| Phase | Scope | Comms model |
|-------|-------|-------------|
| 2 | XIAO bring-up (GPIO tester) | USB HID |
| 3 | `ai_companion` shield: 4 buttons + encoder | BLE HID (test keycodes) |
| 4 | OLED status + haptic notifications | (local, no new comms) |
| 5 | AI-event protocol Mac <-> device | BLE HID + custom GATT (proposed) |

## Communication design (Phase 5 — PROPOSED, not implemented)

Approach under investigation (to be validated in Phase 5):

- Reuse ZMK's BLE connection. Evaluate **ZMK Studio's GATT/RPC** mechanism as a
  transport reference, but do **not** assume it fits AI use cases directly.
- Define a small AI-event protocol carrying at minimum:
  - Session ID
  - Session status (e.g. Running / Waiting-permission / Done / Error)
  - Permission requests (Mac -> device)
  - Physical button responses (device -> Mac: Yes / No / Open / Voice)
  - Notification events (Mac -> device: Permission / Done / Timeout / Error)
- Build a minimal Mac-side test app to inject simulated AI events and log
  device responses for end-to-end verification.

> Open question: HID-only vs HID + custom GATT. Decision deferred to Phase 5
> after inspecting ZMK's BLE HID architecture and ZMK Studio's GATT service.

## Display states (Phase 4 — PROPOSED)

Running / Permission / Done / Error — driven by AI events once the comms channel
exists; until then they can be exercised via local test triggers.

## Haptic (Phase 4 — PROPOSED)

Non-blocking notification for: Permission / Done / Timeout / Error. Must not
block the input/BLE event loop.
