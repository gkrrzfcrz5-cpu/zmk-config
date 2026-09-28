# DRV2605L Haptic — Implementation Plan (Phase 4b)

> Verification level: **RESEARCH / CODE-REVIEW** (evidence gathered from source;
> nothing built or flashed yet). Sources cited inline. Facts that could not be
> confirmed from a primary file are flagged **[UNVERIFIED]**.

## Goal
Make the ERM motor on the Adafruit-design DRV2605L breakout (I2C 0x5A) vibrate on
a trigger (initially a key press, later a host command). "Just vibrate."

## Key finding: no existing driver
- Zephyr `v3.5.0+zmk-fixes` has **no** `drivers/haptics/`, no `drv2605` driver,
  no `ti,drv2605` binding, no Kconfig. Verified by full clone
  (`git@github.com:zmkfirmware/zephyr.git@v3.5.0+zmk-fixes`): `find -iname
  '*drv2605*'` → nothing; only hit is a doc link to an open Zephyr issue #22486.
- Zephyr I2C subsystem IS present (`drivers/i2c/`), so we do **raw I2C register
  writes** via `<zephyr/drivers/i2c.h>` (`i2c_reg_write_byte_dt`, `i2c_write_dt`).

## Approach: custom out-of-tree ZMK behavior module in this repo
The ZMK v0.3 reusable build workflow (`build-user-config.yml@v0.3`) auto-detects a
**`zephyr/module.yml` at the repo root** and passes the repo as
`-DZMK_EXTRA_MODULES=<repo>`. So we add custom C to *this* repo (no separate repo).

### Files to add (repo root)
```
zmk-config/
├─ zephyr/module.yml          # enables the module
├─ CMakeLists.txt             # zephyr_library() + sources_ifdef
├─ Kconfig                    # menuconfig ZMK_BEHAVIOR_HAPTIC
├─ dts/bindings/behaviors/
│  └─ aic,behavior-haptic.yaml
└─ src/behavior_haptic.c
```

`zephyr/module.yml` (shape verified from `zmk/app/module/zephyr/module.yml`):
```yaml
build:
  cmake: .
  kconfig: Kconfig
  settings:
    dts_root: .
```
`config/west.yml` is still required (already present); no edit needed for an
in-repo module.

`CMakeLists.txt`:
```cmake
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_ZMK_BEHAVIOR_HAPTIC src/behavior_haptic.c)
```

`Kconfig`:
```kconfig
menuconfig ZMK_BEHAVIOR_HAPTIC
    bool "DRV2605 haptic behavior"
    default y
    depends on DT_HAS_AIC_BEHAVIOR_HAPTIC_ENABLED && I2C
```
(compatible `aic,behavior-haptic` → `DT_HAS_AIC_BEHAVIOR_HAPTIC_ENABLED`.)

### Behavior API (verified from ZMK v0.3 source)
- `BEHAVIOR_DT_INST_DEFINE(inst, init_fn, pm, data, config, level, prio, api)` —
  `app/include/drivers/behavior.h:180`. Put one-time DRV2605 init in `init_fn`.
- `on_keymap_binding_pressed(binding, event)` returns `ZMK_BEHAVIOR_OPAQUE` —
  from `app/src/behaviors/behavior_reset.c`, `behavior_key_press.c`.
- `zmk_behavior_get_binding(binding->behavior_dev)` to get the device.
- `struct behavior_driver_api { .binding_pressed, .locality, ... }`;
  use `BEHAVIOR_LOCALITY_CENTRAL`.
- Config carries `struct i2c_dt_spec i2c = I2C_DT_SPEC_INST_GET(n)`.

### Devicetree (in overlay/keymap)
DRV2605 I2C node on the XIAO bus (label `&xiao_i2c`, = `&i2c0`, D4/D5 — same bus
as the OLED; addresses 0x3C vs 0x5A do not clash):
```dts
&xiao_i2c {
    drv2605: drv2605@5a { reg = <0x5a>; };   // node only; no Zephyr driver binds it
};
```
Behavior node (keymap `behaviors { }`):
```dts
haptic: behavior_haptic {
    compatible = "aic,behavior-haptic";
    #binding-cells = <0>;
    // phandle/bus wiring to drv2605 per final binding design
};
```
Then bind `&haptic` to a test key to HW-verify (press → buzz).

Add to `.conf`: `CONFIG_I2C=y` (behavior Kconfig also depends on it).

## DRV2605L register sequence (ERM vibrate)
I2C addr **0x5A**. Registers verified from Adafruit_DRV2605 lib (matches chip):

| Reg | Addr | Use |
|-----|------|-----|
| MODE | 0x01 | STANDBY bit6; MODE[2:0] (0x00 int-trig, **0x05 RTP**) |
| RTPIN | 0x02 | RTP amplitude |
| LIBRARY | 0x03 | 1..5 = ERM libs A..E, 6 = LRA |
| WAVESEQ1..8 | 0x04..0x0B | effect ids; terminate with 0 |
| GO | 0x0C | 1 = play, 0 = stop |
| FEEDBACK | 0x1A | **N_ERM_LRA = bit7** (0 = ERM) |
| CONTROL3 | 0x1D | ERM_OPEN_LOOP = bit5 |

**Power-up default: device boots in STANDBY (MODE 0x01 = 0x40 [UNVERIFIED, TI
SLOS854]); FEEDBACK 0x1A default 0x36 → bit7=0 = ERM [UNVERIFIED, TI SLOS854]).**
Corroborated by Adafruit `init()` writing MODE=0x00 first.

### Recommended: option (b) one-shot buzz (verified end-to-end)
```
0x01 <- 0x00           // exit standby, internal-trigger mode
0x1A <- (0x1A & 0x7F)  // ERM (clear bit7)
0x03 <- 0x01           // LIBRARY 1 (ERM library A)
0x04 <- 0x2F           // WAVESEQ1 = effect 47 "Buzz 1 - 100%"  (0x01 = "Strong Click")
0x05 <- 0x00           // end of sequence
0x0C <- 0x01           // GO -> play (auto-clears)
```
Effect ids 1 ("Strong Click") / 47=0x2F ("Buzz 1") verified from the Adafruit
learn-guide PDF (~p17). Do MODE/ERM/LIBRARY once in `init_fn`; do GO (0x0C=1) on
each press.

### Alternative: option (a) RTP continuous, variable strength
```
setup: 0x1A <- (0x1A & 0x7F); 0x01 <- 0x05   // ERM + RTP mode
start: 0x02 <- 0x7F                           // amplitude ~full
stop:  0x02 <- 0x00
```

## Hardware wiring (to HW-test)
DRV2605L → Grove I2C on the expansion board (shares D4/D5 with OLED):
VIN→3V3, GND→GND, SDA→D4, SCL→D5, IN/TRIG→open, OUT+/OUT-→ERM motor.

## Open items before build
- Confirm final binding design for passing the DRV2605 bus/phandle to the
  behavior (phandle vs `reg` child).
- [UNVERIFIED] TI SLOS854 reset defaults (0x01=0x40, 0x1A=0x36) — not critical if
  we explicitly write MODE/ERM in init, but worth confirming.

## Sources
- Zephyr fork (no driver): `zmkfirmware/zephyr@v3.5.0+zmk-fixes`.
- Behavior API: `zmk@v0.3.0` `app/include/drivers/behavior.h`,
  `app/src/behaviors/behavior_{reset,key_press,soft_off}.c`,
  `app/dts/bindings/behaviors/{zero_param,one_param,zmk,behavior-reset}.yaml`.
- Module skeleton: `zmk@v0.3.0` `app/module/{zephyr/module.yml,CMakeLists.txt,
  Kconfig}`, `app/module/drivers/sensor/max17048/{CMakeLists.txt,Kconfig}`.
- Build workflow module detection: `zmk@v0.3.0`
  `.github/workflows/build-user-config.yml`.
- DRV2605 registers/effects: `adafruit/Adafruit_DRV2605_Library` +
  local `MRA154A-drv2605-haptic-controller-breakout.pdf` (Adafruit learn guide).
