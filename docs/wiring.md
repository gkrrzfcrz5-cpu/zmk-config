# AI Companion — Physical Wiring Diagram

There are TWO physical builds. The **COLOUR build** below is the current active
hardware (bare XIAO nRF52840 **Plus** + Waveshare 1.9" ST7789V2 panel + jumper
wires). The **MONO build** further down is the older 0.96" OLED on the Seeed
Expansion Board.

---

# COLOUR build — bare XIAO nRF52840 Plus + Waveshare 1.9" ST7789V2

> Verification level: **HW-VERIFIED** for display (panel lit, 12 screens push)
> and encoder (rotation scrolls the Mac; push reports `ENC`), 2026-09-30.
> Buttons Voice/Yes/No/Open and the DRV2605L haptic: input path proven (D0→VOICE
> over CDC) but the 4 physical keycaps + 3 haptic cues are **not yet HW-VERIFIED**.
>
> Pin source: `boards/shields/ai_companion_color/ai_companion_color.overlay`,
> cross-checked against Seeed's `Adafruit_nRF52_Arduino`
> `variants/Seeed_XIAO_nRF52840_Plus/variant.cpp` `g_ADigitalPinMap[]`.
> Pins are the XIAO **Plus** silkscreen labels `Dn`.

| XIAO Plus pin | nRF pin | Function | Bus / mode | Connect to |
|---------------|---------|----------|------------|-----------|
| D0  | P0.02 | Voice button | direct GPIO, active-low + internal pull-up | button → GND |
| D2  | P0.28 | Yes button   | direct GPIO, active-low + internal pull-up | button → GND |
| D9  | P1.14 | No button    | direct GPIO, active-low + internal pull-up | button → GND |
| D10 | P1.15 | Open button  | direct GPIO, active-low + internal pull-up | button → GND |
| **D1** | **P0.03** | **Encoder SW (push) — 5th key** | direct GPIO, active-low + internal pull-up | encoder SW → GND |
| D7  | P1.12 | Encoder A (SIGA) | EC11 sensor | encoder A |
| D6  | P1.11 | Encoder B (SIGB) | EC11 sensor | encoder B |
| **D4** | **P0.04** | **Haptic SDA** | I2C (`xiao_i2c`) | DRV2605L SDA |
| **D5** | **P0.05** | **Haptic SCL** | I2C (`xiao_i2c`) | DRV2605L SCL |
| D8  | P1.13 | Screen CLK (SCK) | SPI (`xiao_spi`, 8 MHz) | panel CLK |
| D11 | P0.15 | Screen DIN (MOSI) | SPI (MOSI moved here off native P1.15=D10) | panel DIN |
| D12 | P0.19 | Screen CS | SPI chip-select | panel CS |
| D13 | P1.01 | Screen DC | command/data select | panel DC |
| D17 | P1.07 | Screen RST | reset | panel RST |

Power rails: encoder VCC → **3V3** (module pulls A/B to VCC, do NOT use 5V);
DRV2605L VIN → 3V3, OUT+/OUT- → motor (never a GPIO); panel VCC + BL → 3V3
(backlight always on); all GND → GND; panel MISO not connected (write-only).

**Haptic SDA/SCL — read this:** `SDA → D4 (P0.04)`, `SCL → D5 (P0.05)`. Do not
swap them or the DRV2605L is silently unresponsive (no buzz). **On the COLOUR
build the I2C bus has ONLY the DRV2605L on it** — the OLED is gone (replaced by
the SPI panel) — so the old "OLED works but motor doesn't = swapped SDA/SCL"
tell no longer applies here; if the motor is dead, meter continuity D4→SDA and
D5→SCL directly and confirm the DRV2605L answers at I2C 0x5A.

**Avoided / free pins:** D14/D15 (P0.09/P0.10 = NFC — need
`CONFIG_NFCT_PINS_AS_GPIO=y`), D16 (P0.31 = VBAT sense). Free/spare: D3 (P0.29),
D18 (P1.05), and now D19 (P1.03) — vacated when the encoder SW moved to D1.

> **Why the encoder SW is on D1, not D19:** D19 (P1.03) is a **back-side pad** in
> the Plus's D11–D19 group. At 2026-09-30 bring-up the 5th key would not register
> even when D19 was shorted straight to GND, while a top-header pin (D0) fired
> instantly. Jumper contact to the tiny back pads is unreliable, so the switch
> was moved to the free top-header pin **D1 (P0.03)** (commit that moved it also
> needed the firmware fix adding the `ENC` case in `aic_comm_send_input()`).

---

# MONO build — XIAO on the Seeed Expansion Board (0.96" OLED)

> Mounting decision (user, 2026-09-28): the XIAO **plugs into the Seeed XIAO
> Expansion Board** so the board's on-board 0.96" OLED can be used. Buttons,
> encoder and the DRV2605L are wired to the board's Grove connectors / broken-out
> male header (the board "leads out" every XIAO pin). Pin numbers are the XIAO
> silkscreen labels (`Dn`); the expansion board just routes them to easy points.

> Verification level: **CODE-REVIEW / DESIGN**. On-board pin usage is confirmed
> from the Seeed wiki source (`Seeeduino-XIAO-Expansion-Board.md`). Nothing is
> HW-VERIFIED yet.

## What the expansion board already uses (from the Seeed wiki)

| XIAO pin | On-board peripheral | Note |
|----------|---------------------|------|
| D1 | User button | avoided (trigger polarity unconfirmed) |
| D2 | microSD CS | reused (no SD card in use) |
| D3 / A3 | Passive buzzer | avoided (piezo loads the line; removable by cutting a trace) |
| D4 / D5 | I2C: OLED (0x3C) + RTC | used for OLED + DRV2605L |
| D8 / D9 / D10 | microSD SPI (SCK / MISO / MOSI) | D9/D10 reused, D8 left spare (no SD card) |

Grove connectors on the board: **I2C ×2** (D4/D5), **UART ×1** (D6/D7),
**A0/D0 ×1** (D0/D1). All pins are also brought out to a male header.

## Pin allocation (this project)

| XIAO pin | Function | Where to connect on the board |
|----------|----------|-------------------------------|
| D0 | Voice button | Grove A0/D0 (signal) → button → GND |
| D2 | Yes button | header D2 → button → GND (SD CS, unused) |
| D9 | No button | header D9 → button → GND (SD MISO, unused) |
| D10 | Open button | header D10 → button → GND (SD MOSI, unused) |
| D7 | Encoder SIGA (A) | Grove UART |
| D6 | Encoder SIGB (B) | Grove UART |
| D4 (SDA) | I2C data | OLED (on-board) + DRV2605L (Grove I2C) |
| D5 (SCL) | I2C clock | OLED (on-board) + DRV2605L (Grove I2C) |
| D1 / D3 / D8 | (avoided / spare) | D1 on-board button, D3 buzzer, D8 SD SCK |

## Per-component wiring

### 4 buttons (Voice / Yes / No / Open)
Each button = a momentary switch between the pin and GND. No external resistor
(firmware enables the nRF internal pull-up, active-low).

| Button | XIAO pin | Board access |
|--------|----------|--------------|
| Voice | D0 | Grove A0/D0 connector |
| Yes | D2 | male header |
| No | D9 | male header |
| Open | D10 | male header |

### Rotary encoder (4-pin module: SIGA / SIGB / VCC / GND) — rotation only
The module has on-board 3.3k pull-ups to VCC, so **VCC must be 3V3** (not 5V).
It has no push switch on the header, so only rotation is used → mouse-wheel scroll.

| Module pin | XIAO / rail | Board access |
|------------|-------------|--------------|
| SIGA (1) | D7 | Grove UART |
| SIGB (2) | D6 | Grove UART |
| VCC (3) | 3V3 | Grove UART VCC (confirm it is 3V3) |
| GND (4) | GND | Grove UART GND |

> Note: a Grove connector is VCC/GND + 2 signal pins, which matches the encoder's
> 4 pins — a Grove-to-Dupont cable works, or jumper to the header.

### OLED display (SSD1306, I2C, 0x3C) — Phase 4
On-board. Nothing to wire — it is connected to D4/D5 by the expansion board.

### Haptic driver (Adafruit DRV2605L breakout, I2C, 0x5A) — Phase 4
Plug into a **Grove I2C** connector (shares D4/D5 with the OLED). The breakout is
the motor driver — the motor goes to OUT+/OUT-, never straight to a GPIO. EN is
pulled high on the board (no enable GPIO). On-board 10k I2C pull-ups.

| Breakout pin | Connect to | Note |
|--------------|-----------|------|
| VIN | 3V3 (Grove I2C VCC) | battery-ready |
| GND | GND | |
| SDA | D4 (Grove I2C) | shared bus |
| SCL | D5 (Grove I2C) | shared bus |
| IN/TRIG | (leave open) | not used in I2C mode |
| OUT+ / OUT- | motor +/− | **ERM** motor (non-polar) |

## Shared I2C bus (D4 / D5)
OLED (0x3C) + DRV2605L (0x5A) on the same two wires, distinguished by address.
Pull-ups already present (board + DRV2605L breakout). Don't add more.

> **Hand-wiring gotcha (HW-confirmed 2026-09-28):** if you jumper the DRV2605L
> instead of using a Grove cable, do **not** swap SDA/SCL. `SDA→D4`, `SCL→D5`.
> Swapping them makes the DRV2605L silently unresponsive (no buzz) while the OLED
> keeps working (it is wired correctly on the same bus) — so "OLED lights up but
> motor never buzzes, VIN reads 3.3 V" is the signature of a swapped SDA/SCL on
> the haptic board. Tip: I2C is a shared bus, so you can tap the DRV2605L's
> SDA/SCL onto the exact points the OLED already uses.

## Assumptions baked into this plan
- **No microSD card** is used → D2 / D9 / D10 are free for buttons.
- **Buzzer not used** → D3 avoided, no trace cutting needed.
- Motor is **ERM**.

## Power
- Now: USB-C (via the XIAO or the expansion board's USB-C).
- Later: Li-ion on the expansion board's battery connector (it has a charge chip).
  Every peripheral is powered from **3V3** (the 5V pin is USB-only).

## Open items
- None blocking. (OLED source resolved: use the expansion board's on-board OLED.)
