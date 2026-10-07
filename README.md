# Grok Bot Companion

ESP-IDF thin-client firmware for Frank Wagner’s **Grok Bot Companion** on  
**Waveshare ESP32-S3-Touch-AMOLED-1.75**.

Repo: https://github.com/roguespark-04/grokbot-companion

## Status

| Item | State |
| --- | --- |
| Project skeleton | **Done** (ESP-IDF layout + stubs) |
| Board hardware | **Not arrived** — no on-device bring-up yet |
| Meridian wake contract | **Locked** — see [WEBHOOK_CONTRACT.md](WEBHOOK_CONTRACT.md) |
| Audio relay | **v1 ready** in [relay/](relay/) — Flask + persistent jobs; needs Meridian webhook URL |
| Vertical slice | PTT → capture → upload → poll reply → play → status face (**stubs**) |

## Hardware

- [Product page](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)
- [Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
- [Official GitHub + HARDWARE_REFERENCE](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75)

| Piece | Detail |
| --- | --- |
| MCU | ESP32-S3R8, 8 MB PSRAM, 16 MB Flash |
| Display | 1.75″ AMOLED 466×466, CO5300 (QSPI), touch CST9217 (I2C) |
| Audio | ES7210 dual-mic + AEC; ES8311 I2S playback; 8 Ω 2 W speaker |
| Power | AXP2101; RTC PCF85063; IMU QMI8658; IO expander TCA9554 |
| PTT v1 | **BOOT** (GPIO0) or **PWR** (TCA9554 EXIO4) |
| Camera | **Not in v1** |

Full GPIO map: [PINOUT.md](PINOUT.md).

## Architecture (thin client)

Device does **not** run Grok Bot. Device talks **only** to an audio relay (Tailscale):

1. Press-to-talk → capture WAV (16 kHz mono PCM)
2. `POST /upload` to relay → `{ job_id, audio_url, result_url }`
3. Relay POSTs JSON-only wake to Meridian (“Muse Charm wake”); **sender key stays on relay**
4. Device polls `GET /result/:job_id` → play reply WAV
5. Status face: idle / listening / thinking / speaking / error

No media bytes on the Meridian webhook. Details: [WEBHOOK_CONTRACT.md](WEBHOOK_CONTRACT.md).

## Milestone (first vertical slice)

When the board arrives, wire Waveshare demos into the stubs:

1. Display face from `05_LVGL_WITH_RAM` (CO5300)
2. Capture/playback from `06_I2SCodec` / BSP (ES7210 + ES8311)
3. Wi-Fi + relay upload/poll against Spark’s Tailscale relay
4. BOOT as PTT; face tracks state machine in `main/main.c`

## Build / flash (ESP-IDF)

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/) (v5.x recommended; match Waveshare demo IDF when bringing up hardware).

```bash
cd /workspace/grokbot-companion-push   # or your clone path
idf.py set-target esp32s3
idf.py menuconfig          # Muse Charm Configuration → Wi-Fi + relay URL
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`sdkconfig.defaults` enables 16 MB flash, octal PSRAM, USB Serial/JTAG console.

**Secrets:** put Wi-Fi and **device↔relay** auth in `sdkconfig` / NVS — never commit.  
Meridian webhook sender key belongs in `relay/.env` only.

Download mode if flash fails: hold **BOOT**, tap **RESET**, release RESET, release BOOT.

## Tree

```
grokbot-companion/
  CMakeLists.txt
  sdkconfig.defaults
  README.md
  WEBHOOK_CONTRACT.md
  PINOUT.md
  ARDUINO_NOTES.md
  main/
    CMakeLists.txt
    Kconfig.projbuild
    main.c                  # state machine
    *_stub modules...
    include/
  relay/                    # Audio relay v1 (shared box; not on-device)
```

## Arduino

Primary tree is ESP-IDF. Thin notes: [ARDUINO_NOTES.md](ARDUINO_NOTES.md).

## Open questions (Frank / Meridian / Spark)

- ~~Final product & repo name~~ → **Grok Bot Companion** / `grokbot-companion`
- Spark relay host (Tailscale hostname) and device auth scheme (HMAC vs bearer)
- Meridian → relay reply write mechanism (callback vs other)
- Max PTT utterance length / upload size
