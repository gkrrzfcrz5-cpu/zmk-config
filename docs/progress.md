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
## Phase 3 — ai_companion shield  ⏳ IN PROGRESS (2026-09-28)
Completed:
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
  - Mounting = **discrete wiring** (jumpers straight to XIAO; expansion board
    NOT used as carrier). Full pin freedom, matches the firmware.
  - DRV2605L = **Adafruit breakout** (schematic confirmed): EN tied high on-board
    → dropped the D9 EN reservation; motor on OUT+/OUT-. "Just vibrate" only.
  - Encoder = **4-pin module** (SIGA/SIGB/VCC/GND); push switch is NOT on the
    header → **encoder push dropped**, rotation only. Firmware updated to 4
    buttons + rotation (removed D6 kscan key, 4-col transform, removed `&kp P`).
  - Full wiring recorded in `docs/wiring.md`.
- Earlier BUILD (BUILD): run `36362523013` (5-key scroll version) succeeded;
  a rebuild is pending after the 4-key / rotation-only change.

Open items (need the user, hardware):
- OLED source: standalone I2C module vs reuse the expansion board's OLED (the
  discrete choice means the board's soldered-on OLED isn't directly usable).
- Motor type: ERM vs LRA (for the DRV2605L config; default assume ERM).

Parts status: XIAO ✅, expansion board + OLED ✅, DRV2605L + motor ✅ in hand;
physical buttons ⛔ (test via jumper-to-GND), rotary encoder ⛔ (not yet arrived).

Next executable task (user, hardware):
- Flash `seeeduino_xiao_ble-ai_companion.uf2`; touch D0..D3 to GND → expect
  V/Y/N/O typed. Encoder rotation (→ scroll) verified once the knob arrives.

Known risk (Phase 4): DRV2605L has no ZMK/Zephyr 3.5 driver → will need a small
custom I2C driver / behaviour. OLED (SSD1306) is natively supported.
## Phase 4 — Display + haptic  ⏳ NOT STARTED
## Phase 5 — MacBook communication  ⏳ NOT STARTED
