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
- **BLE HW-VERIFIED on ai_companion firmware ✅ (2026-09-28):** the device
  advertises as **"AI Companion"**, pairs + connects to macOS, and delivers HID
  **input over BLE** (proven with a temporary `&out OUT_TOG` build: switch the
  endpoint to BLE with USB as power only → a jumpered key still typed on the Mac,
  so it went wirelessly). Gotchas solved: old `ZMK Tester` bond survives a UF2
  flash (cleared via a temporary `&bt BT_CLR`); macOS caches the BLE name by
  address (scan list shows the old name until you connect / `sudo pkill
  bluetoothd`); ZMK routes HID to USB by default when USB is plugged (force BLE
  with `&out OUT_BLE`/`OUT_TOG` or run on battery). Done on a throwaway
  `ble-test` branch (not merged); main keeps the clean V/Y/N/O keymap.

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
## Phase 4 — Display + haptic  ✅ HW-VERIFIED (2026-09-28)
Completed:
- **DRV2605L haptic HW-VERIFIED ✅ (2026-09-28):** with the Open button (D10)
  temporarily bound to `&haptic`, a press fired a clear ERM buzz on the real
  motor. Root cause of an initial "no buzz": the hand-wired DRV2605L had
  **SDA/SCL swapped** (the OLED kept working on the same I2C bus and VIN read
  3.3 V — the tell-tale signature of a swapped SDA/SCL on the haptic board);
  fixed by `SDA→D4`, `SCL→D5`. The Open button is now restored to `&kp O`; the
  haptic will be host-driven in Phase 5.
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
  artifact `firmware` 360,157 B. (HW-verified above.)
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

Next executable task:
- Phase 5: draft the device↔host interface contract (English, shareable) and
  agree the transport (BLE GATT vs USB HID vs USB CDC) + message schema with the
  co-working Mac/AI-side team, then implement the device side.
## Phase 4d — Custom status-screen UI (mono prototype)  ✅ BUILD (2026-09-28)
While the colour 1.9" ST7789 panel + XIAO nRF52840 Plus ship (~2 days), built a
mono prototype of the "12 Core Screen States" UI on the in-hand SSD1306 (128x64,
1bpp). Branch `mono-ui-prototype`.
- Replaced ZMK's built-in status screen with a custom LVGL screen:
  `CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM=y` (a `choice`; auto-deselects
  BUILT_IN) + a strong `zmk_display_status_screen()` in `src/status_screen.c`
  overriding ZMK's weak stub (app/src/display/main.c).
- `src/status_screen.c`: renders text-forward, mono-adapted versions of all 12
  states (HOME / SESSIONS / WORKING / NEED YOU / PERMISSIONS / PERMISSION / DONE
  / ATTENTION / TIMEOUT / LISTENING / PROCESSING / OPENING), header = state name
  + wifi glyph, footer where it fits, `lv_bar` progress on WORKING, bordered
  Deny/Allow chips on PERMISSION. No host link yet (Phase 5), so a 5s `lv_timer`
  AUTO-CYCLES through the 12 states so all can be reviewed on hardware. (v1 is
  auto-cycle only; Open-button manual advance deferred — input hooking to a
  custom screen not yet verified.)
- `CMakeLists.txt`: compile the source into ZMK's `app` target (NOT a
  zephyr_library — a lazily-linked archive member would not win the weak-symbol
  override) under `if(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)`; the tester_xiao
  build (no CUSTOM) skips it.
- `ai_companion.conf`: select CUSTOM + LVGL label/bar + montserrat 10/12/14
  (default 12); `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=n` so the demo stays lit.
- **BUILD gotcha:** first CI run (36402801095) FAILED — `status_screen.c`
  included only `<lvgl.h>` but used `ARG_UNUSED`, which lives in a Zephyr header.
  Fixed by adding `#include <zephyr/kernel.h>`. (Unknown CONFIG names only warn;
  a hard exit-1 = a C compile error → tester passing localised it to this file.)
- CI run `36403320578` = success; artifact `firmware` 366,303 B.
- **RENDER BUG → FIXED (HW-VERIFIED, 2026-09-28):** the first flashes showed the
  whole SSD1306 as garbage ("全是亂碼"), and a stripped-down 3-label diagnostic
  screen (identical in pattern to ZMK's known-good built-in) was *also* garbage
  — proving the fault was NOT the layout code but a lower-layer config lost when
  switching from BUILT_IN to CUSTOM. **Root cause:** ZMK's Kconfig sets
  `LV_Z_MEM_POOL_SIZE default 4096 if ZMK_DISPLAY_STATUS_SCREEN_BUILT_IN`
  (app/src/display/Kconfig) — that 4096-byte LVGL heap default is dropped the
  moment CUSTOM is selected, so LVGL fell back to a much smaller base pool. With
  too little heap, LVGL's draw/glyph allocations fail mid-render and the panel
  fills with garbage. **Fix:** `CONFIG_LV_Z_MEM_POOL_SIZE=4096` in
  `ai_companion.conf` (commit `42d9a0a`, CI run `36408304523`). Flashed as fw12
  (546,304 B) → the 3-label diagnostic rendered cleanly on hardware (user
  confirmed). Also re-added `CONFIG_LV_USE_THEME_MONO=y` (only `imply`d under
  BUILT_IN) — kept, since a 1bpp custom screen still needs the mono theme.
  Diagnostic lesson: a minimal screen that mirrors the built-in exactly isolates
  layout bugs from pipeline/config bugs — when both fail, the config is at fault.
- **Full 12-state UI restored (commit `372a1d6`):** brought back the complete
  text-forward prototype now that the heap is sized right; `aic_render()` calls
  `lv_obj_clean()` on each state so heap use stays bounded across the 5s cycle.
  CI run `36424395661` = success; artifact `firmware` 596,480 B.
- **HW-VERIFIED ✅ (2026-09-28):** flashed as fw13; all 12 states render cleanly
  and auto-cycle every 5s on the on-board SSD1306 (user confirmed "都有了").
  The mono prototype of the "12 Core Screen States" is complete on real
  hardware. Next: per-screen layout tweaks on request, then port to the colour
  1.9" ST7789 panel when it + the XIAO nRF52840 Plus arrive.

## Phase 5 — MacBook communication  ⏳ NOT STARTED