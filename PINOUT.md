# Pinout — Waveshare ESP32-S3-Touch-AMOLED-1.75

Authoritative source (fetched 2026-10-06):

- [HARDWARE_REFERENCE.md](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
- BSP header: `firmware/brookesia/components/waveshare__esp32_s3_touch_amoled_1_75/include/bsp/esp32_s3_touch_amoled_1_75.h`
- Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75
- Amazon: https://www.amazon.com/Waveshare-ESP32-S3-Development-Dual-core-Microphones/dp/B0F7XTJ7JW

Firmware macros: `main/include/board_pins.h`.

## Display (CO5300, QSPI) — 466×466

| Signal | GPIO |
| --- | ---: |
| LCD_DATA0..3 | 4, 5, 6, 7 |
| LCD_CS | 12 |
| LCD_TE | 13 |
| LCD_PCLK | 38 |
| LCD_RST | 39 |

No GPIO backlight — brightness via CO5300 panel commands.

## Touch (CST9217, I2C)

Prefer **CST9217** (demos / HARDWARE_REFERENCE). Wiki once mentions FT3168; ignore for this board.

| Signal | GPIO |
| --- | ---: |
| TP_INT | 11 |
| TP_RST | 40 |
| I2C | SDA 15 / SCL 14 @ `0x5A` |

## I2C bus (shared)

SCL **GPIO14**, SDA **GPIO15**, typically 400 kHz.

| Addr (7-bit) | Device |
| ---: | --- |
| `0x18` | ES8311 playback codec |
| `0x20` | TCA9554 IO expander |
| `0x34` | AXP2101 PMIC |
| `0x40` | ES7210 mic / AEC ADC |
| `0x51` | PCF85063 RTC |
| `0x5A` | CST9217 touch |
| `0x6B` | QMI8658 IMU |

## I2S audio

Shared clocks; ES8311 STD TX + ES7210 4-slot TDM RX.

| Signal | GPIO | Role |
| --- | ---: | --- |
| MCLK | 42 | ES8311 + ES7210 |
| BCLK | 9 | shared |
| LRCK/WS | 45 | shared |
| DOUT | 8 | ESP → ES8311 (playback) |
| DIN | 10 | ES7210 → ESP (capture) |
| PA_CTRL | 46 | NS4150B amp (prefer BSP API) |

ES7210 inputs: MIC1+MIC2 = onboard mics; MIC3 = ES8311 AEC reference; MIC4 N/C.  
TDM serialize order: MIC1, MIC3(ref), MIC2, MIC4.

Board BSP voice profile: 24 kHz; pocket companion **upload** target: **16 kHz mono WAV** (downsample/reconfig as needed).

## TF / microSD

Waveshare BSP uses **1-bit SDMMC** (not wiki’s older SPI wording):

| Signal | GPIO |
| --- | ---: |
| CMD | 1 |
| CLK | 2 |
| D0 | 3 |
| D3/CS | 41 (wired; unused in 1-bit mount) |

## Buttons (confirmed mapping)

| Control | Connection | Role |
| --- | --- | --- |
| **BOOT** | GPIO0, active low | **Press-and-hold PTT** (confirmed by Frank). Hold = capture audio; release = stop capture and start upload→relay flow. Wired in `ptt_button.c` via `MUSE_BOOT_BUTTON`. |
| **PWR** | AXP2101 path; `SYS_OUT` on TCA9554 **EXIO4** (high=pressed) | **Power on/off only.** Board/PMIC (AXP2101) custom PWR behavior — leave power management to the board. Firmware does **not** use PWR for PTT. Document only; no firmware PTT wiring. |

Touch-screen PTT (CST9217) is deferred; not part of v1 button mapping.

## Expansion header (2.54 mm 8-pin)

1 VBUS · 2 GND · 3 3V3 · 4 GPIO44/U0RXD · 5 GPIO43/U0TXD · 6 GPIO17 · 7 GPIO18 · 8 GPIO16

## Demo paste targets

| Need | Waveshare path |
| --- | --- |
| LVGL / display | `examples/esp-idf/05_LVGL_WITH_RAM` |
| I2S codec | `examples/esp-idf/06_I2SCodec` |
| PMIC | `examples/esp-idf/01_AXP2101` |
| Arduino audio | `08_ES8311` |
