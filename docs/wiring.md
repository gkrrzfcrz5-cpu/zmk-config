# AI Companion — Physical Wiring Diagram

> Mounting decision (user, 2026-09-28): **discrete wiring** — every part is wired
> to the XIAO with jumper wires; the Seeed expansion board is NOT used as a
> carrier. Pin numbers below are the XIAO silkscreen labels (`Dn`).

> Verification level: this document is **CODE-REVIEW / DESIGN** only. Pinout is
> per the Seeed XIAO official layout + the parts' own schematics (Adafruit
> DRV2605L breakout REV-A; the user's 4-pin EC11 encoder module). Nothing here is
> HW-VERIFIED yet — confirm against the physical silkscreen before soldering.

## XIAO nRF52840 pin map (USB-C at top)

```
            ┌───────  USB-C  ───────┐
   D0 / A0  │ ●                   ● │  5V   (USB VBUS only; dead on battery)
   D1 / A1  │ ●                   ● │  GND
   D2 / A2  │ ●                   ● │  3V3
   D3 / A3  │ ●                   ● │  D10 / MOSI
D4 / A4/SDA │ ●                   ● │  D9  / MISO
D5 / A5/SCL │ ●                   ● │  D8  / SCK
   D6 / TX  │ ●                   ● │  D7  / RX
            └───────────────────────┘
     (BAT+ / BAT- solder pads on the back, for a Li-ion cell later)
```

## Pin allocation

| XIAO pin | Function | Notes |
|----------|----------|-------|
| D0 | Voice button | to GND, internal pull-up (active-low) |
| D1 | Yes button | to GND, internal pull-up |
| D2 | No button | to GND, internal pull-up |
| D3 | Open button | to GND, internal pull-up |
| D4 (SDA) | I2C data | shared: OLED + DRV2605L |
| D5 (SCL) | I2C clock | shared: OLED + DRV2605L |
| D7 | Encoder A / SIGA | EC11 rotation |
| D8 | Encoder B / SIGB | EC11 rotation |
| D6 | spare | (encoder push dropped — module has no SW pin) |
| D9 | spare | (DRV2605L breakout ties EN high on-board) |
| D10 | spare | |
| 3V3 | power rail (+) | powers OLED, DRV2605L, encoder module |
| GND | ground rail | common for everything |

## Per-component wiring

### 4 buttons (Voice / Yes / No / Open)
Each button is a simple momentary switch between the pin and GND. No external
resistor (the firmware enables the nRF internal pull-up).

| Button | Pin A | Pin B |
|--------|-------|-------|
| Voice | D0 | GND |
| Yes | D1 | GND |
| No | D2 | GND |
| Open | D3 | GND |

### Rotary encoder (4-pin module: SIGA / SIGB / VCC / GND)
Rotation only — the module does **not** break out the push switch. The module has
on-board 3.3k pull-ups to VCC, so **VCC must be 3V3** (not 5V) to keep the signal
swing within the nRF's 3.3V logic level.

| Module pin | XIAO |
|------------|------|
| SIGA (1) | D7 |
| SIGB (2) | D8 |
| VCC (3) | **3V3** |
| GND (4) | GND |

Turning the knob = mouse-wheel scroll (scrolls the focused Mac window). Direction
is fixable in firmware / macOS "natural scrolling" after testing.

### OLED display (SSD1306, I2C, addr 0x3C)  — Phase 4
> ⚠️ Open item: the discrete mounting choice means the expansion board's on-board
> OLED (soldered to that board) can't be used. This assumes a **standalone 4-pin
> I2C OLED module**. Decide: buy a standalone module, or plug into the expansion
> board just for the OLED.

| OLED pin | XIAO |
|----------|------|
| VCC | 3V3 |
| GND | GND |
| SDA | D4 |
| SCL | D5 |

### Haptic driver (Adafruit DRV2605L breakout, I2C, addr 0x5A) — Phase 4
Purpose: just make the motor buzz on command. The breakout is itself the motor
driver — the motor connects to OUT+/OUT-, never straight to a GPIO. EN is pulled
high on the board (R2), so no enable GPIO is needed. On-board 10k I2C pull-ups.

| Breakout pin | XIAO / part | Notes |
|--------------|-------------|-------|
| VIN | 3V3 | battery-ready (5V is USB-only) |
| GND | GND | |
| SDA | D4 | shared I2C |
| SCL | D5 | shared I2C |
| IN/TRIG | (leave open) | not used in I2C mode |
| OUT+ | motor + | |
| OUT- | motor − | motor is non-polar; swapping only flips spin dir |

> Open item: motor type (**ERM** vs **LRA**) — needed for the DRV2605L config.
> Default assumption = ERM (typical small vibration motor) until confirmed.

## Shared I2C bus (D4 / D5)
OLED (0x3C) and DRV2605L (0x5A) sit on the same two wires, distinguished by
address. Pull-ups are already present on the DRV2605L breakout (10k). Do not add
more unless a bus scan shows problems.

## Power
- Now: powered over USB-C.
- Later (final design): Li-ion cell on the BAT+/BAT- pads; the XIAO charges it
  over USB. On battery, only 3V3 is live (the 5V pin is USB VBUS) — which is why
  every peripheral above is powered from **3V3**, not 5V.

## Open items (need the user)
1. OLED source: standalone I2C module, or reuse the expansion board's OLED?
2. Motor type: ERM or LRA?
