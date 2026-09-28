# Progress Log

Date format: YYYY-MM-DD. Newest phase last.

## Recorded development environment (Phase 0, 2026-09-27)

| Tool | Version | Notes |
|------|---------|-------|
| macOS | 26.6 (25G5028f) | Apple M2, arm64 |
| git | 2.50.1 (Apple Git-155) | global user: `lin_mickey <lin_mickey@apple.com>` |
| Python | 3.12.9 | |
| uv | 0.11.16 | |
| Homebrew | 7.0.6 | |
| node / npm | v26.5.0 / 11.17.0 | |
| Docker | 29.7.2 | daemon not running (not needed for cloud build) |
| gh | — | not installed |
| west/cmake/ninja/dtc/arm-gcc | — | not installed (cloud build strategy) |

Toolchain strategy: **GitHub Actions cloud build** — no local Zephyr SDK, per
project preference. Local machine only needs git (SSH) to push.

Network constraint: HTTPS to `github.com` is blocked by the sandbox; **git uses
SSH** (`git@github.com`, key `~/.ssh/id_ed25519`, authenticated as
`gkrrzfcrz5-cpu`). `api/codeload/raw.githubusercontent/zmk.dev` are reachable.

## Phase 0 — Inspect environment ✅ DONE (2026-09-27)
- Full environment inspected and recorded (see table above + `test-results.md`).
- Confirmed clean start: no pre-existing ZMK/Zephyr workspace to preserve.

## Phase 1 — Set up ZMK  ✅ DONE (2026-09-27)
Completed:
- Verified authoritative sources by cloning:
  - `zmkfirmware/unified-zmk-config-template` (pins **ZMK v0.3**).
  - `zmkfirmware/zmk` **@ v0.3.0** (commit `edf5c08`, Zephyr `v3.5.0+zmk-fixes`).
- Scaffolded `zmk-config` from the official template (verbatim template files +
  authored `build.yaml`, `README.md`, `docs/`).
- **Version-compat finding:** plan's `xiao_ble//zmk` (Zephyr 3.6+ syntax) does
  NOT exist on v0.3. Verified correct id is **`seeeduino_xiao_ble`**. Using it.
- Repo pushed to `git@github.com:gkrrzfcrz5-cpu/zmk-config.git` (SSH).
- **Verification gate PASSED:** Actions run #1 (`36325932901`) = success (~3.5
  min); artifact `firmware` (139,472 B, contains `.uf2`) produced.

## Phase 2 — XIAO bring-up  ⏳ IN PROGRESS (2026-09-27)
Completed:
- **Compilation done & verified**: `seeeduino_xiao_ble` + `tester_xiao` builds
  successfully in CI (see run #1). This satisfies the "compiles + UF2 produced"
  part of the Phase 2 gate.
- **Flash verified (FLASH)**: UF2 flashed via bootloader; board enumerates as
  USB "ZMK Tester".
- **BLE compiled-in verified (BUILD)**: decoded the flashed `.uf2` — it links the
  Nordic BLE controller + GATT HID. Board-level `app/boards/seeeduino_xiao_ble.conf`
  (`CONFIG_ZMK_BLE=y`) overrides the shield's `def_bool n`; the earlier
  "tester disables BLE" note was corrected.
- **BLE pairing verified (HW-VERIFIED)**: device advertises as `ZMK Tester`;
  just-works pairing to the Mac succeeded (user-observed, 2026-09-27).

Blocked / needs user (hardware):
- Run the GPIO short-to-GND test (D0..D10 -> types `PIN n`) — needs a jumper
  wire / paperclip / any conductor. Deferred until user has one on hand.

Next executable task:
- Phase 3: research XIAO pinout and propose the `ai_companion` shield GPIO
  allocation (4 buttons + rotary encoder + push) for review BEFORE implementing.
## Phase 3 — ai_companion shield  ✅ HW-VERIFIED (buttons) (2026-09-28)
Completed:
- **FLASH ✅ (2026-09-28):** `seeeduino_xiao_ble-ai_companion.uf2` flashed via
  bootloader; board re-enumerates as USB **"AI Companion"** (VID 0x1D50 / PID 0x615E).
- **Buttons HW-VERIFIED ✅ (2026-09-28):** user jumpered each pin to GND and the
  correct keycode was typed over USB HID — D0→`v` (Voice), D2→`y` (Yes),
  D9→`n` (No), D10→`o` (Open). Test string observed: `vvvvyyyyynnnnnnooo`.
  All 4 buttons + the expansion-board pin re-allocation are confirmed on real hardware.
- Encoder rotation (→ mouse-wheel scroll) still ⛔ NOT TESTED — encoder not in hand.

- Proposed + user-approved pin allocation: D0 Voice / D1 Yes / D2 No / D3 Open /
  D7 encoder-A(SIGA) / D8 encoder-B(SIGB); D4/D5 reserved for I2C
  (OLED SSD1306 + DRV2605L), D6/D9/D10 spare.
- Authored `boards/shields/ai_companion/` (overlay, keymap, conf, Kconfig.*,
  zmk.yml) to verified ZMK v0.3 patterns (`reviung5` + `tester_xiao`).
- Mapping: buttons V/Y/N/O (placeholder test keycodes),
  **encoder rotate = mouse-wheel scroll** (`&msc SCRL_UP/DOWN`).
  Scroll is the REAL function: it scrolls the focused Mac window (the Optimus
  session window), handled natively by macOS HID — no host app needed.
- **Hardware bring-up decisions (2026-09-28, from the user's parts):**
  - Mounting = **plug into the Seeed XIAO Expansion Board** (to use its on-board
    OLED). The board pre-commits pins (confirmed from the Seeed wiki source):
    D1 user button, D2 SD CS, D3 buzzer, D4/D5 I2C (OLED+RTC), D8/D9/D10 SD SPI.
  - Re-allocated to avoid conflicts (no SD card / no buzzer used): buttons
    Voice=D0 (Grove A0/D0), Yes=D2, No=D9, Open=D10 (SD lines reused); encoder
    SIGA=D7, SIGB=D6 (Grove UART); D4/D5 I2C = OLED (on-board) + DRV2605L
    (Grove I2C). D1/D3 avoided, D8 spare.
  - DRV2605L = **Adafruit breakout** (schematic confirmed): EN tied high on-board
    → no enable GPIO; motor on OUT+/OUT-. Motor = **ERM** (confirmed).
  - Encoder = **4-pin module** (SIGA/SIGB/VCC/GND), no push switch on header →
    **rotation only** (no encoder-push). VCC must be 3V3 (on-board 3.3k pull-ups).
  - Full wiring recorded in `docs/wiring.md` (expansion-board version).
- Earlier BUILD: run `36366129167` (4-key, discrete-wiring pins) succeeded; a
  rebuild is pending after the expansion-board pin re-allocation.

Open items (need the user, hardware):
- None blocking. OLED source resolved (use the expansion board's on-board OLED);
  motor type resolved (ERM).

Parts status: XIAO ✅, expansion board + OLED ✅, DRV2605L + motor ✅ in hand;
physical buttons ⛔ (test via jumper-to-GND), rotary encoder ⛔ (not yet arrived).

Next executable task (user, hardware):
- Encoder rotation (→ scroll) verified once the knob arrives.
- Phase 4: OLED (SSD1306, native) status display + DRV2605L (ERM) haptic
  (needs a small custom I2C driver — no ZMK/Zephyr 3.5 driver exists).

Known risk (Phase 4): DRV2605L has no ZMK/Zephyr 3.5 driver → will need a small
custom I2C driver / behaviour. OLED (SSD1306) is natively supported.
## Phase 4 — Display + haptic  ⏳ IN PROGRESS (2026-09-28)
Completed:
- **Encoder rotation → scroll HW-VERIFIED ✅ (2026-09-28):** turning the encoder
  scrolls the focused macOS window, correct direction (run `36381989367`).
  Debug path: rotation hardware first proven with a volume-diagnostic build
  (`&inc_dec_kp C_VOLUME_UP/DOWN` moved macOS volume), isolating HW from the
  scroll path. Root cause of "no scroll": `&msc` is `zmk,behavior-input-two-axis`,
  a VELOCITY behavior (distance = speed x hold-time, accrued per trigger-period
  tick); a sensor-rotate tap (5ms) is shorter than the 16ms tick → ~0 movement.
  Fix in the keymap: `#define ZMK_POINTING_DEFAULT_SCRL_VAL 60` before pointing.h,
  `tap-ms = <50>` on the sensor-rotate, and override `&msc` to trigger-period-ms
  10 / time-to-max-speed-ms 0 / delay-ms 0 → ~3 wheel units per detent (tunable).
- **DRV2605L haptic BUILD ✅ (2026-09-28):** implemented a custom out-of-tree ZMK
  behavior `aic,behavior-haptic` (`src/behavior_haptic.c`) that fires a one-shot
  ERM buzz (effect 47, "Buzz 1 - 100%") via raw I2C register writes — there is no
  Zephyr DRV2605 driver. The node at `drv2605@5a` on `&xiao_i2c` is BOTH the I2C
  device and the behavior; the keymap references `&haptic`. Module wiring:
  `zephyr/module.yml` (cmake/kconfig/dts_root), `Kconfig` (ZMK_BEHAVIOR_HAPTIC),
  `CMakeLists.txt`, DT binding `aic,behavior-haptic.yaml`. Two build fixes:
  (1) add ZMK `app/include` to the module include path (ZMK keeps it PRIVATE to
  its own `app` target) so `<drivers/behavior.h>` resolves;
  (2) init at `CONFIG_APPLICATION_INIT_PRIORITY` (after the I2C controller) so
  the build-time `check_init_priorities` passes. CI run `36375309425` = success;
  artifact `firmware` 360,157 B. **TEMPORARY:** the Open button (D10) is bound to
  `&haptic` for HW testing — restore `&kp O` after the motor is HW-verified.
- **OLED HW-VERIFIED ✅ (2026-09-28):** flashed to the XIAO on the expansion
  board; the on-board SSD1306 shows the ZMK built-in status screen (battery
  widget + "AI Compani…" device name). It blanks on idle (ZMK `blank-on-idle`
  default for SSD1306) and wakes on input activity — expected behaviour, good for
  the battery design. Whether to keep blank-on-idle vs always-on / wake-on-notify
  is a Phase 5 design decision (deferred).
- **OLED BUILD ✅ (2026-09-28):** added the expansion board's on-board SSD1306
  (128x64 @ 0x3C) to the ai_companion shield. Overlay = `ssd1306@3c` on
  `&xiao_i2c` (= &i2c0, D4/D5), `compatible = "solomon,ssd1306fb"`,
  multiplex-ratio 63, all required props; `chosen zephyr,display = &oled`.
  conf = `CONFIG_ZMK_DISPLAY=y` + LVGL 1bpp mono settings. Modelled verbatim on
  ZMK v0.3 kyria. CI run `36370715642` = success; artifact `firmware` 359,942 B.
  Uses the built-in ZMK status screen (layer/battery/output widgets).

Next executable task (user, hardware):
- Download the `firmware` artifact from run `36375309425`, flash the ai_companion
  uf2, wire the DRV2605L (Grove I2C: VIN→3V3, GND→GND, SDA→D4, SCL→D5) with an
  ERM motor on OUT+/OUT−, press the Open button (D10) → motor buzzes (HW-VERIFY).
- After haptic HW-verify: restore `&kp O` for the Open button; merge Phase 4.
## Phase 5 — MacBook communication  ⏳ NOT STARTED
