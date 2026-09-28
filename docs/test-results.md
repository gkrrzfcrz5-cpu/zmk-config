# Test Results & Verification Evidence

> Verification levels are **not** interchangeable:
> **CODE-REVIEW** < **BUILD (compiles)** < **FLASH (on device)** < **HW-VERIFIED (observed behavior)**.
> Each claim below records which level it reached and the evidence.

## Legend
- ✅ PASS — evidence recorded
- ⛔ NOT TESTED — not yet performed
- ❌ FAIL — evidence recorded

---

## Phase 0 — Environment inspection

| Check | Result | Evidence |
|-------|--------|----------|
| OS / arch | ✅ macOS 26.6 (25G5028f), Apple M2, arm64 | `uname -a`, `sw_vers` |
| git | ✅ 2.50.1 | `git --version` |
| Python / uv | ✅ 3.12.9 / uv 0.11.16 | `python3 --version`, `uv --version` |
| Homebrew / node | ✅ brew 7.0.6 / node v26.5 | `brew --version`, `node --version` |
| gh (GitHub CLI) | ⛔ not installed | `which gh` -> not found |
| west / cmake / ninja / dtc / arm-gcc | ⛔ not installed (cloud build used instead) | `which ...` |
| Existing ZMK/Zephyr workspaces | ✅ none found (clean start) | `find ~ ... -iname 'zmk*'` |
| GitHub SSH auth (personal) | ✅ `gkrrzfcrz5-cpu` authenticated | `ssh -T git@github.com` -> "Hi gkrrzfcrz5-cpu!" |
| Network: HTTPS to github.com | ❌ blocked/timeout (SSH used instead) | `curl https://github.com` -> timeout |
| Network: api/codeload/raw/zmk.dev | ✅ reachable | `curl` 200/301 |
| XIAO board connected | ⛔ not connected (not required this phase) | no USB serial / no UF2 volume |

---

## Phase 1 — ZMK config repository

| Item | Level reached | Evidence |
|------|---------------|----------|
| Config scaffolded from official template | ✅ CODE-REVIEW | files copied verbatim from `zmkfirmware/unified-zmk-config-template` |
| Board/shield identifiers verified against v0.3 source | ✅ CODE-REVIEW | `seeeduino_xiao_ble.zmk.yml`, `tester_xiao.zmk.yml` |
| GitHub repo `zmk-config` created | ✅ DONE | `git@github.com:gkrrzfcrz5-cpu/zmk-config.git`, push `main` OK |
| GitHub Actions build succeeds | ✅ BUILD | run #1 `36325932901` = **success** (~3.5 min) |
| UF2 artifact produced | ✅ BUILD | artifact `firmware` = 139,472 bytes (contains `.uf2`) |

Run URL: https://github.com/gkrrzfcrz5-cpu/zmk-config/actions/runs/36325932901
Jobs (all success): `Fetch Build Keyboards`, `Build (seeeduino_xiao_ble, tester_xiao, seeeduino_xiao_ble-tester_xiao)`, `Merge Output Artifacts`.

---

## Phase 2 — XIAO bring-up

| Item | Level reached | Evidence |
|------|---------------|----------|
| `seeeduino_xiao_ble` + `tester_xiao` compiles | ✅ BUILD | run #1 build job success + artifact (see above) |
| UF2 file validated (magic + family id) | ✅ | magicStart0/1 + magicEnd OK; familyID 0xADA52840 (nRF52840); appStart 0x27000; 717 blocks / 367104 B |
| UF2 flashed to board | ✅ FLASH | copied to `/Volumes/XIAO-SENSE`; drive auto-ejected + rebooted; now enumerates as USB **"ZMK Tester" (ZMK Project)** |
| Board identity | ✅ | INFO_UF2.TXT: Model "Seeed XIAO nRF52840", Board-ID `Seeed_XIAO_nRF52840_Sense`, bootloader 0.6.1, SoftDevice S140 v7.3.0 |
| GPIO test (D0..D10 short-to-GND types `PIN n`) | ⛔ NOT TESTED | requires hardware (no jumper/conductor on hand yet) |
| BLE stack compiled into flashed firmware | ✅ BUILD | decoded flashed `.uf2`: links Nordic BLE controller (`zephyr/subsys/bluetooth/controller/ll_sw/nordic/`) + `BT_`/`GATT`/`peripheral` symbols; BLE name `ZMK Tester` present. `CONFIG_ZMK_BLE=y` confirmed present. |
| Plain vs `-ble` build identical | ✅ | both `.uf2` byte-identical, SHA-256 `8ba209…5380` → the flashed tester already contains BLE |
| BLE pairing to Mac | ✅ HW-VERIFIED | device appears as `ZMK Tester` in macOS System Settings → Bluetooth; just-works pairing succeeded (2026-09-27, user-observed). |

> **Corrected finding (2026-09-27):** an earlier note claimed `tester_xiao`
> disables BLE by design (`Kconfig.defconfig def_bool n`). That is wrong for this
> board. The board-level fragment `app/boards/seeeduino_xiao_ble.conf`
> (`CONFIG_ZMK_BLE=y`, `CONFIG_ZMK_USB=y`), which Zephyr auto-loads for the
> `seeeduino_xiao_ble` board, **overrides** the shield's `def_bool n`. Proof: the
> flashed firmware links the Nordic BLE link-layer controller and exposes a BLE
> HID device. No separate BLE variant or reflash is needed — the tester already
> does USB HID + BLE HID simultaneously.

---

## Phase 3 — ai_companion shield (4 buttons + EC11 encoder)

| Item | Level reached | Evidence |
|------|---------------|----------|
| Shield authored to verified v0.3 patterns | ✅ CODE-REVIEW | overlay/keymap modelled on `reviung5` (direct kscan + `alps,ec11`) + `tester_xiao` (`seeed_xiao` / `&xiao_d`) |
| `seeeduino_xiao_ble` + `ai_companion` compiles | ✅ BUILD | run `36367806134` (expansion-board pins), job **Build (…, ai_companion, …) = success** |
| UF2 flashed to board (via bootloader volume) | ✅ FLASH | copied to `/Volumes/XIAO-SENSE`; volume auto-ejected + rebooted; re-enumerated as USB **"AI Companion"** (VID 0x1D50 / PID 0x615E) |
| Buttons type test keycodes (D0/D2/D9/D10 → V/Y/N/O) | ✅ HW-VERIFIED | user jumpered each pin to GND → typed `v`/`y`/`n`/`o` respectively (2026-09-28, over USB HID) |
| Encoder rotate (D7/D6 → mouse-wheel scroll) | ✅ HW-VERIFIED | 2026-09-28: turning scrolls the focused macOS window, correct direction (run `36381989367`). Rotation hardware first proven via a volume-diagnostic build (`&inc_dec_kp C_VOLUME_UP/DOWN` changed macOS volume). Scroll then fixed: `&msc` is a velocity behavior so a sensor-rotate tap accrued ~0 → raised `ZMK_POINTING_DEFAULT_SCRL_VAL`=60, `tap-ms`=50, `&msc` trigger-period-ms=10 / time-to-max-speed-ms=0 / delay-ms=0 (~3 wheel units per detent). |
| BLE + USB on ai_companion firmware | ✅ BUILD | same board as tester → board conf enables BLE+USB; device name "AI Companion" |

> Pin allocation (expansion-board mount; see `docs/wiring.md`):
> D0 Voice (Grove A0/D0), D2 Yes, D9 No, D10 Open (SD lines reused, no SD card),
> D7 Enc-SIGA, D6 Enc-SIGB (Grove UART); D4/D5 I2C (on-board OLED + DRV2605L via
> Grove I2C). D1 (on-board button) + D3 (buzzer) avoided, D8 spare. On-board pin
> usage confirmed from the Seeed wiki source.

---

## Phase 4 — Display + haptic

| Item | Level reached | Evidence |
|------|---------------|----------|
| On-board SSD1306 OLED (128x64 @ 0x3C) compiles into ai_companion | ✅ BUILD | run `36370715642`, job **Build (…, ai_companion, …) = success**; artifact `firmware` 359,942 B. Node modelled on ZMK v0.3 kyria (`solomon,ssd1306fb`, mux-ratio 63) on `&xiao_i2c` (D4/D5). |
| OLED shows the built-in status screen | ✅ HW-VERIFIED | flashed; XIAO on expansion board; screen shows battery widget + "AI Compani…" (device name). Blanks on idle (ZMK `blank-on-idle` default for SSD1306), wakes on input activity. (2026-09-28) |
| DRV2605L haptic (motor vibrates) | ✅ HW-VERIFIED | run `36375309425` built the custom out-of-tree ZMK behavior `aic,behavior-haptic` (raw I2C register writes; no Zephyr DRV2605 driver). Fixes en route: add ZMK `app/include` to the module include path; init at `CONFIG_APPLICATION_INIT_PRIORITY` so `check_init_priorities` passes. **HW-VERIFIED 2026-09-28:** with the Open button temporarily bound to `&haptic`, a press fired a clear ERM buzz. Root cause of an initial "no buzz": the hand-wired DRV2605L had **SDA/SCL swapped** (OLED kept working on the same bus, VIN read 3.3 V — the swap signature); fixed by `SDA→D4`, `SCL→D5`. Open button then restored to `&kp O`. |

---

_No results above are assumed. Rows move to ✅/❌ only when the evidence exists._
