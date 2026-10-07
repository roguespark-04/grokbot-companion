# Grok Bot Companion

ESP-IDF thin-client firmware for Frank Wagner's **Grok Bot Companion**, a pocket device on the
**Waveshare ESP32-S3-Touch-AMOLED-1.75** that talks to all of his Grok Bots.

Repo: https://github.com/roguespark-04/grokbot-companion

## Status

| Item | State |
| --- | --- |
| Firmware | **Builds clean** (ESP-IDF v5.5.1, BSP `waveshare/esp32_s3_touch_amoled_1_75` 3.0.1, LVGL 9.4) |
| Board hardware | **Not arrived.** Codec, I2S, PMIC, and RTC paths are marked `HARDWARE TODO` |
| Multi-bot touch UI | **Done in code**: minimal full-screen avatar home, infinite carousel, bot panel, voice picker (28 app voices), device panel, sleep/lock |
| Wake contract | v1 **locked**; v1.1 multi-bot fields; v1.2 direct per-bot wakes + `wake_route`. See [WEBHOOK_CONTRACT.md](WEBHOOK_CONTRACT.md) |
| Audio relay | **Live** on the shared box, public HTTPS via Tailscale Funnel (`https://grokbot-box.tail4de099.ts.net`). Direct per-bot wakes with Meridian fallback. See [relay/](relay/) |
| Buttons | **BOOT** = hold-to-talk (and wake from lock); **PWR** = power on/off (AXP2101) |

## Mockups

Rendered from the same layout numbers and shape code as `main/ui/` by
[`docs/mockups/render_mockups.py`](docs/mockups/render_mockups.py). Shapes and colors follow
each bot's app profile (avatarShape / avatarColor); hex values are approximate until checked
against app screenshots.

![Home: Meridian idle, Photon working, Pulse listening, Scribe speaking](docs/mockups/01_home_carousel.png)

| Mid-swipe (wraps Nexus → Meridian) | Swipe up: bot panel | Voice picker | Swipe down: device | Sleep / lock |
| --- | --- | --- | --- | --- |
| ![](docs/mockups/01b_carousel_midswipe.png) | ![](docs/mockups/02_bot_panel.png) | ![](docs/mockups/03_voice_picker.png) | ![](docs/mockups/04_device_settings.png) | ![](docs/mockups/05_sleep_lock.png) |

Also: single home frames `docs/mockups/01_home_{meridian_idle,photon_working,pulse_listening,scribe_speaking}.png`,
the swipe sequence [01b_carousel_wrap_sequence.png](docs/mockups/01b_carousel_wrap_sequence.png),
how panels open from home [07_navigation_map.png](docs/mockups/07_navigation_map.png),
the scrolled panel [02b](docs/mockups/02b_bot_panel_scrolled.png), and every bot in every state
[06_avatar_states_sheet.png](docs/mockups/06_avatar_states_sheet.png).

## Touch UI (`main/ui/`)

**Home is minimal:** only the current bot's avatar (filling ~86 % of the round screen), its
name, and its relay status line, overlaid on the lower avatar over a soft dark gradient.
No clock, Wi-Fi, battery, page dots, hints, or buttons. Those live behind gestures. A
conversation being open shows only as a soft accent glow along the screen edge.

| Gesture | Where | Result |
| --- | --- | --- |
| Swipe ← / → | anywhere on home | Next / previous bot. **Infinite circle** (wraps both ways, no end stop). The current avatar slides, fades, and shrinks out while the next slides in; neighbours are invisible at rest. The new name fades in |
| Swipe ↑ | starting at the **bottom edge** (lowest ~90 px) | Per-bot panel |
| Swipe ↓ | starting at the **top edge** (top ~90 px) | Device settings (time, Wi-Fi, battery, firmware, device ID) |
| Swipe ↓ / tap grabber | top of the bot panel or voice picker | Close |
| Swipe ↑ / tap grabber | bottom of the device panel | Close |

Optional `MUSE_UI_EDGE_HINTS` (default off) shows a faint grabber only while a finger is on
the top or bottom edge.

* **Per-bot panel**: talk mode (**Press to talk** default / **Always listen**),
  Start/End conversation, **Voice**, volume, brightness, auto-sleep.
* **Voice picker**: a scrollable roller of the Grok Bot app's 28 voices (xAI grok-tts ids
  from `GET /voices`). Stored per bot in NVS (`voice_<bot_id>`), default from the roster
  (Clay = Cosmo, others = Eve). The device sends the id; the reply side synthesizes with it.
  Preview is enabled only when the relay has a clip (optional, TODO).
* **Horizontal swipes inside panels never change the bot**, and the carousel is locked
  during a voice turn.
* **Avatars** (`ui_avatar.c`): one draw callback paints the shape (blob, teardrop, cloud,
  hex, squircle, circle), eyes, and effects. idle = breathing + blink (blob/cloud outlines
  slowly morph), listening = ripples, thinking = dots orbiting the rim, working = faster
  accent orbit, speaking = level-driven scale + waveform mouth, error = shake + red. Dark
  bodies get a light rim. The renderer is a vtable, so a sprite renderer can replace it.

### Bots (from the relay, `GET /bots`)

Meridian (gray blob) · Spark (TBD) · Quark (TBD) · Scribe (black teardrop, light rim) ·
Photon (yellow teardrop) · dr eggbot (red teardrop) · Pulse (blue cloud) · Proton (green hex) ·
Clay (brown squircle) · Nexus (violet blob). Spark and Quark use a neutral placeholder until
we have a screenshot of their app look. Edit `relay/bots.json` (palette + per-bot `avatar`);
no reflash needed.

## Talking

| Mode | How a turn starts | Ends |
| --- | --- | --- |
| **Press to talk** (default) | Hold BOOT (≥ `MUSE_PTT_HOLD_MS`) | Release BOOT |
| **Always listen** | While a conversation is open, the energy VAD (`vad.c`) detects speech. ~300 ms of pre-roll is kept | ~800 ms of silence, or the max length |

BOOT works as PTT in both modes. Each turn: LISTENING → UPLOADING → THINKING/WORKING (the
status line follows the relay's `GET /status`) → SPEAKING → IDLE. The upload carries
`target_bot_id`, `voice_id`, and `conversation_id`. Meridian routes the turn to the chosen bot.

## Sleep & lock (`power_mgr.c`)

```
AWAKE --idle >= auto-sleep--> DIMMED (4 s, "Sleeping" hint) --> LOCKED
  ^                              |                               |
  +---- touch / BOOT / busy -----+                               |
  +---- BOOT short press (< 300 ms): screen on, NO recording <---+
  +---- BOOT hold (>= 300 ms): wake + LISTENING immediately <----+
```

* **Idle** means two things. First, nothing is active: no turn uploading, thinking, or
  speaking, no always-listen conversation open, and the current bot's status isn't thinking
  or working (stale statuses expire after 10 min). Second, no touch or button input for the
  auto-sleep timeout (default **30 s**; "Never" disables sleep).
* **Locked**: AMOLED brightness goes to 0 and the LVGL worker and tick are paused
  (`esp_lv_adapter_pause`). The touch indev is disabled, so touch is locked. The mic powers
  down and Wi-Fi switches to `WIFI_PS_MAX_MODEM` but stays associated, so status polls resume
  quickly. BOOT (GPIO0, low level) is the wake source, and the app task blocks so FreeRTOS
  tickless idle can enter **automatic light sleep** (`CONFIG_PM_ENABLE`).
* **Wake**: the mic is re-armed and recording starts before the screen comes back. A
  **short press** discards that audio and just shows the last bot. A **hold** past
  `MUSE_WAKE_LONG_PRESS_MS` (300 ms) becomes a voice turn with the first words kept.
  Releasing uploads as usual. The wake press never doubles as a PTT edge.
* **Awake**: BOOT is plain hold-to-talk, as before. **PWR** stays power on/off (PMIC).
* HARDWARE TODO for the one-charge-a-day goal: CO5300 sleep-in plus AMOLED rail gating,
  CST9217 sleep, then measuring real mA through the AXP2101.

## Hardware

- [Amazon (ASIN B0F7XTJ7JW)](https://www.amazon.com/Waveshare-ESP32-S3-Development-Dual-core-Microphones/dp/B0F7XTJ7JW) · [Product page](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm) · [Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75) · [Waveshare GitHub](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75)

| Piece | Detail |
| --- | --- |
| MCU | ESP32-S3R8, 8 MB PSRAM, 16 MB flash |
| Display | 1.75″ AMOLED 466×466, CO5300 (QSPI), CST9217 touch (I2C) |
| Audio | ES7210 dual mic + AEC, ES8311 playback, 8 Ω 2 W speaker |
| Power / misc | AXP2101 PMIC (battery 1000 mAh planned), PCF85063 RTC, QMI8658 IMU, TCA9554 |
| Camera | Not in v1 |

Full GPIO map: [PINOUT.md](PINOUT.md).

## Build / flash

Requires **ESP-IDF ≥ 5.5** (the BSP v3 needs it). The component manager pulls the BSP, LVGL
9.4, the CO5300/CST9217 drivers, and esp_lvgl_adapter.

```bash
idf.py set-target esp32s3
idf.py menuconfig     # "Grok Bot Companion Configuration": Wi-Fi, relay URL, device token
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Key Kconfig: `MUSE_UPLOAD_URL` (default `https://grokbot-box.tail4de099.ts.net`, TLS verified with the ESP-IDF certificate bundle), `MUSE_DEVICE_RELAY_TOKEN`, `MUSE_DEVICE_ID`,
`MUSE_WAKE_LONG_PRESS_MS` (300), `MUSE_DIM_BEFORE_SLEEP_MS` (4000), `MUSE_PREROLL_MS` (300),
`MUSE_MAX_UTTERANCE_S` (20), `MUSE_TZ` (Chicago), and `MUSE_CAPTURE_STUB_SILENCE` (exercise the
relay path before the mic is wired). Custom `partitions.csv` uses a 6 MB factory app.

**Secrets**: Wi-Fi and the device↔relay token go in `sdkconfig`/NVS. Never commit them. The
Meridian sender key lives only in `relay/.env`.

Download mode: hold **BOOT**, tap **RESET**, release RESET, then release BOOT.

## Tree

```
main/
  main.c              app state machine: events, turns, always-listen VAD, sys task
  power_mgr.c         idle → dim → lock (light sleep) → BOOT short/long wake
  settings_store.c    NVS settings + per-bot voice + roster cache
  bot_registry.c      /bots + /voices JSON → structs (built-in fallback)
  webhook_client.c    relay HTTP: upload (routing headers), result poll, status, JSON GET
  audio_capture.c     pre-roll ring + turn buffer + WAV export (ES7210 read = TODO)
  audio_playback.c    reply download + level meter (ES8311 write = TODO)
  vad.c               energy VAD (always-listen)
  power_info.c        AXP2101 battery / charging
  rtc_time.c          PCF85063 + SNTP
  wifi_net.c          STA + reconnect + RSSI + power save
  ptt_button.c        BOOT debounce, edges, light-sleep wake ISR
  ui/                 LVGL 9 UI: ui.c, ui_home.c (carousel), ui_bot_panel.c,
                      ui_voice_picker.c, ui_device_panel.c, ui_gesture.c, ui_avatar.c
relay/                Flask relay on the shared box (bots.json, voices.json)
docs/mockups/         PNG mockups + renderer
```

## Open questions

- Exact avatar colors and Spark/Quark looks: waiting on app screenshots (`relay/bots.json` palette).
- Reply-side xAI grok-tts backend (voice ids are already the app's); preview clips are TODO.
- Talk mode is device-wide today, while voice is per bot. Should talk mode be per bot too?
- Always-listen only listens while a conversation is open.
- Unicode in status text would need a bigger font (the relay forces ASCII today).
