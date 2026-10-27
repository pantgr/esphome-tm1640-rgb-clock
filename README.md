# esphome-tm1640-rgb-clock

ESPHome firmware for the **CAI-274 RGB LED clock kit** ("CAI-274-V4.3" on the PCB, a DIY electronic clock kit
sold on AliExpress). The kit's STC8G1K17 microcontroller and DS1302 RTC are left out; an **ESP32-S3** drives the
display board's **TM1640B** through the empty DIP-16 footprint of the MCU (U3). No change on the clock's board
besides the wires into that footprint.

What it does:
- HH:MM with a blinking colon, time from SNTP (no RTC, no buttons needed)
- 7 fixed colours, a colour **wave** running across the digits, and four **per-segment** effects: ring, rows,
  sweep, confetti
- the chip's 8 brightness steps for the whole display
- a temperature on the small display, received over ESP-NOW from another ESP (optional)
- everything from Home Assistant through the ESPHome API

Sibling project: [esphome-aip1640-clock](https://github.com/pantgr/esphome-aip1640-clock), a monochrome wooden
clock with the AiP1640 (the TM1640's twin).

## Read this first: only hard colours
The TM1640B is an appliance-panel chip: **one bit per LED** and **one pulse width for the whole display**. It scans
its 16 grids by itself on an internal RC clock (450 kHz typical, no scan timing in the datasheet). The kit wires
each big digit as three grids, one per colour, so every segment can be red, green, blue or any mix of them fully on:
red, green, blue, yellow, cyan, magenta, white.

In-between colours (orange, pink, smooth fades) would need PWM per LED. Done from outside, by rewriting the chip's
RAM fast with dithered on/off frames, it **beats against the chip's own scan and flickers**: tried with delta-sigma
and with random dithering, both flicker. The chip gives you no way to see or sync to its scan. So this firmware
uses steady hard colours only, and makes them interesting by giving **every segment its own colour**. Steady colours
do not flicker.

If you want real colour mixing, use a driver that the microcontroller scans itself (for example the AiP33628 of
the HU-058 kit, see [HU-058_ESPHome](https://github.com/misterblack1/HU-058_ESPHome)). The AiP33628 has the same
8 x 16 matrix but a **different SOP28 pinout**: it is not a drop-in replacement for the TM1640B.

## Hardware
- **ESP32-S3 DevKitC-1 clone** (tested: N16R8). Leave `flash_size` unset: on this board `flash_size: 16MB`
  boot-looped (rst:0x3 about every second). Logger on `UART0` = the CH343 USB-C.
- **Level shifter** (bidirectional module, BSS138 type): the TM1640B needs VIH >= 0.7 x VDD and its DIN/SCLK are
  pulled up to the clock's VDD (~4.6-5 V) by 10k on the board. Never wire them straight to the ESP.
- Wiring into the U3 footprint (STC pinout from the kit's schematic):

| ESP32-S3 | level shifter | U3 pin | kit signal |
|---|---|---|---|
| GPIO4 | LV1 -> HV1 | 1 (P1.0) | TM1640B SCLK |
| GPIO5 | LV2 -> HV2 | 2 (P1.1) | TM1640B DIN |
| 3V3 | LV | - | |
| - | HV | 6 (VDD) | clock VDD |
| GND | GND | 8 (GND) | GND |

- Leave the STC and the DS1302 out of their footprints while the ESP is wired.
- **One supply at a time**: either the clock's USB-C or the ESP's USB, not both.
- Unused U3 pins, for whoever wants them: 3 thermistor divider, 4 photoresistor divider (both sit on VDD: never
  straight to an ESP pin), 5/7/13 DS1302 RST/SCLK/IO, 10 buzzer, 12/15/11/9 buttons S1-S4, 16 vibration switch,
  14 USB-power sense.

## Display map (kit schematic, confirmed on the board)
- GRID1..12 = digit 1..4 x red, green, blue (RAM address = 3 x digit + colour).
- SEG1..7 = segments a..g, SEG8 = the mark next to each digit: digit 1 upper colon dot, digit 2 lower colon dot,
  digit 3 PM, digit 4 Alarm.
- Small display, one fixed colour per LED: GRID13 bit0/bit1 = upper/lower half of the hundreds "1", bit2 F,
  bit3 C, bit4 calendar, bit5 date, bit6 hourglass; GRID14 tens, GRID15 ones (a..g). GRID16 not connected.

## Install
1. Copy `examples/rgb-clock.yaml` and `examples/secrets.yaml.example` (as `secrets.yaml`), fill in the secrets.
2. `external_components` points at `../components`; or use the GitHub source shown in the yaml.
3. Set your timezone and, if you have a sender, its MAC in `espnow:`; otherwise remove the ESP-NOW block.
4. `esphome run rgb-clock.yaml` (first time over USB).
5. Boot check in the log: `TM1640RGB:` with CLK GPIO4, DIO GPIO5, half bit 1 us and a growing frame count.

## Entities
| type | name | what |
|---|---|---|
| select | colour | red / green / blue / yellow / cyan / magenta / white / wave / ring / rows / sweep / confetti |
| number | brightness | 0..7, the chip's pulse width 1/16 .. 14/16 for the whole display |
| number | cycle seconds | 1..600 s each colour of `wave` stays |
| number | step ms | 50..2000 ms per step of the segment effects |
| light | board rgb | the DevKitC's own WS2812 (optional) |

The effects:
- **wave**: one colour per digit, red -> yellow -> green -> cyan -> blue -> magenta, running across the face.
- **ring**: a rainbow turning around each digit (a..f clockwise).
- **rows**: colours rolling from top to bottom.
- **sweep**: a rainbow running left to right over 13 columns (three per digit, the colon its own column).
- **confetti**: a random colour per segment every step.

## ESP-NOW temperature
Any ESP can send it, every 30 s or so. Payload: byte 2 = `'T'` (0x54), bytes 3..4 = temperature in 0.1 C,
int16 little endian; bytes 0..1 are ignored (free for e.g. `'H'` + humidity). The clock shows whole degrees
0..199 with the C mark; no reading for 10 min, or below 0, leaves it dark.

## The component: `tm1640rgb`
```yaml
tm1640rgb:
  id: tm
  clk_pin: GPIO4
  dio_pin: GPIO5
  half_bit_us: 1   # 1..50, default 2; SCLK max 1 MHz
```
From lambdas:
- `id(tm).set_digit_rgb(d, r_mask, g_mask, b_mask)`: digit 0..3, one segment mask per colour (bit0..6 = a..g,
  bit7 = the mark), fully on. This is the steady one.
- `id(tm).set_digit(d, segs, r, g, b)`: segments plus 0..255 per colour. 0 and 255 are steady; anything in
  between is dithered and flickers (see above).
- `id(tm).set_small(i, bits)`: GRID13..15, plain bits.
- `id(tm).brightness(0..7)`.

A FreeRTOS task on core 0 rewrites the 16 RAM bytes in a loop; the display command goes only when the
brightness changes (sent with every frame, the display flickers).

## License
GPL-3.0.
