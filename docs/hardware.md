# Hardware Configuration

> Evidence-first: every pin/interface claim here must cite a verified source
> (datasheet, ZMK/Zephyr board file, or a measurement). Unverified rows are
> marked **PROPOSED** and must not be wired until confirmed.

## MCU: Seeed Studio XIAO nRF52840

- SoC: Nordic **nRF52840** (ARM Cortex-M4F, BLE 5, USB).
- Board id in ZMK v0.3: **`seeeduino_xiao_ble`**
  (source: `app/boards/arm/seeeduino_xiao_ble/seeeduino_xiao_ble.zmk.yml`,
  `exposes: [seeed_xiao]`, `outputs: [usb, ble]`).
- Interconnect: **`seeed_xiao`** — exposes the XIAO edge pins as devicetree
  nexus nodes. In ZMK the digital-pin nexus is referenced as **`&xiao_d`**
  (e.g. `&xiao_d 0` = pin **D0**), as used by the `tester_xiao` shield overlay.

### XIAO digital pin nexus (`&xiao_d`)

The `tester_xiao` overlay configures **D0..D10** (11 pins) as direct GPIO inputs.
This confirms the usable digital pin range for our custom shield.

| Nexus ref    | Silk label | Notes                                  |
|--------------|-----------|----------------------------------------|
| `&xiao_d 0`  | D0        | ADC-capable                            |
| `&xiao_d 1`  | D1        | ADC-capable                            |
| `&xiao_d 2`  | D2        | ADC-capable                            |
| `&xiao_d 3`  | D3        | ADC-capable                            |
| `&xiao_d 4`  | D4        | I2C SDA (default)                      |
| `&xiao_d 5`  | D5        | I2C SCL (default)                      |
| `&xiao_d 6`  | D6        | UART TX (default)                      |
| `&xiao_d 7`  | D7        | UART RX (default)                      |
| `&xiao_d 8`  | D8        | SPI SCK (default)                      |
| `&xiao_d 9`  | D9        | SPI MISO (default)                     |
| `&xiao_d 10` | D10       | SPI MOSI (default)                     |

> The peripheral-function column above is the **standard XIAO pin map** and is
> marked **PROPOSED** until cross-checked against the Seeed XIAO nRF52840
> datasheet/wiki and the exact devicetree pin assignments in the board files
> (to be done in Phase 3 before proposing GPIO allocation).

## Custom shield: `ai_companion` (Phase 3 — not yet designed)

Controls to allocate: 4 buttons (Voice / Yes / No / Open) + rotary encoder
(A/B) + encoder push switch. A conflict-checked GPIO allocation will be proposed
here **before** any wiring. Display + haptic pins are reserved in Phase 4.

**Status: NOT STARTED.** No pin connections are asserted yet.

## Physical connection status

- XIAO board physically connected to the Mac during Phase 0/1: **No** (not
  required yet; confirmed no USB serial device / no UF2 volume present).
