# Arduino notes (secondary)

Primary firmware tree is **ESP-IDF**. Use Arduino only for quick peripheral smoke tests if helpful before porting into the ESP-IDF stubs.

## Board package

- Board manager: **esp32 by Espressif Systems** ≥ 3.1.0  
- Select **ESP32S3 Dev Module**; enable **USB CDC On Boot** when using Type-C serial.

## Libraries (from Waveshare demo bundle)

Offline/online install per wiki. Notable: GFX (CO5300), LVGL 8.4, SensorLib (CST9217/QMI8658/PCF85063), XPowersLib (AXP2101), ESP32_IO_Expander (TCA9554), `Mylibrary` pin macros.

Demo repo: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75

## Useful Arduino demos

| Demo | Use |
| --- | --- |
| `08_ES8311` | Speaker / I2S playback smoke test |
| LVGL + AXP2101 | Display + PWR button behavior |
| `10_Touch_CST9217` | Raw touch (if present in current demos) |

After validating pins/audio in Arduino, port working init sequences into:

- `main/audio_capture.c` / `audio_playback.c`
- `main/display_face.c`

Do **not** put Meridian sender keys in Arduino sketches either — use the relay.
