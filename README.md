# AI Companion — ZMK Firmware (`zmk-config`)

Firmware for a physical **AI Companion** device that connects to a MacBook and
provides physical controls, an auxiliary status display, and notifications so
the user does not have to keep checking the computer while AI coding agents run.

- **MCU:** Seeed Studio XIAO nRF52840
- **Firmware base:** [ZMK](https://zmk.dev) `v0.3.0` (Zephyr `v3.5.0+zmk-fixes`)
- **Host:** MacBook (Apple Silicon), connected over Bluetooth LE (BLE HID today)
- **Build:** GitHub Actions cloud build (no local Zephyr SDK required)

> Status is tracked in [`docs/progress.md`](docs/progress.md). Verification
> evidence (build logs, hardware results) lives in
> [`docs/test-results.md`](docs/test-results.md). Nothing in this repo is
> claimed "verified on hardware" until it appears there.

## Planned hardware controls (Phase 3+)

| Control        | Purpose                                        |
|----------------|------------------------------------------------|
| Voice button   | Start voice input for the current AI session   |
| Yes / No       | Answer an AI permission request                |
| Open button    | Open the corresponding AI session on the Mac   |
| Rotary encoder | Navigate sessions / permission requests        |
| Encoder push   | Confirm a selection                            |
| OLED           | Show status: Running / Permission / Done / Error |
| Haptic         | Notify: Permission / Done / Timeout / Error    |

## Repository layout

```
config/west.yml                 West manifest — pins ZMK v0.3
build.yaml                       GitHub Actions build matrix (board + shield)
.github/workflows/build.yml      Calls zmkfirmware build-user-config.yml@v0.3
zephyr/module.yml                Exposes boards/ as a Zephyr module (board_root: .)
boards/shields/                  Custom shields (ai_companion added in Phase 3)
docs/                            hardware / architecture / test-results / progress
zmk-development/SKILL.md         Reusable, hardware-specific dev workflow (added after bring-up)
```

## Current build target (Phase 2 bring-up)

`build.yaml` builds the official **GPIO tester** so each XIAO pin can be verified
over USB before any custom hardware is wired:

- Board: `seeeduino_xiao_ble`
- Shield: `tester_xiao`

Shorting XIAO digital pin **D0..D10** to **GND** types `PIN <n>` + Enter over USB
HID — this is how each GPIO is confirmed working without extra hardware.

## Build & flash (summary)

1. Push to GitHub → GitHub Actions builds the firmware.
2. Download the `*.uf2` artifact from the workflow run.
3. Double-tap the XIAO **RESET** to enter the UF2 bootloader (a `XIAO-SENSE` /
   `NICENANO`-style USB drive appears).
4. Copy the `.uf2` onto that drive. The board reboots into the new firmware.

Detailed, verified flashing steps are recorded in `docs/test-results.md` as they
are performed on real hardware.

## Development environment

See [`docs/progress.md`](docs/progress.md) for the recorded toolchain versions
and [`zmk-development/SKILL.md`](zmk-development/SKILL.md) (added after the first
successful bring-up) for the reusable workflow.
