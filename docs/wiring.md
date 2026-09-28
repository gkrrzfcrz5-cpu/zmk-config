# AI Companion — Physical Wiring Diagram (Expansion Board version)

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
