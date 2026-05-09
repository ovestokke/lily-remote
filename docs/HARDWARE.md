# Hardware Notes: LILYGO T5 E-Paper S3 Pro Lite

Target board: **LILYGO T5 E-Paper S3 Pro Lite**.

## PlatformIO settings

The project uses a local board definition:

- [`boards/T5-ePaper-S3.json`](../boards/T5-ePaper-S3.json)

This was copied from LILYGO's `Xinyuan-LilyGO/LilyGo-EPD47` `esp32s3` branch. It configures:

- ESP32-S3 MCU
- 240 MHz CPU
- QIO 80 MHz flash
- 16 MB flash
- OPI PSRAM
- Arduino USB CDC mode

## Arduino IDE reference settings

If testing outside PlatformIO, use the vendor-recommended style settings:

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |
| USB CDC On Boot | Enabled |
| CPU Frequency | 240 MHz (WiFi) |
| Flash Mode | QIO 80 MHz |
| Flash Size | 16 MB |
| Partition Scheme | 16M Flash / FATFS-style large flash partition |
| PSRAM | OPI PSRAM |

## Known components

| Component | Notes |
| --- | --- |
| Display | ED047TC1, 4.7", 960×540, 16 grayscale |
| Touch | GT911 at I2C `0x5D` confirmed by scan |
| RTC | PCF85063/PCF8563 family at I2C `0x51` confirmed by scan |
| Battery/power | BQ25896 at I2C `0x6B`, BQ27220 at I2C `0x55`; TPS65185/e-paper power device was seen once at `0x68` but is not consistently visible in later scans |
| IO expander | PCA9535 family at I2C `0x20` confirmed by scan |

## I2C scan notes

Scan on SDA `39`, SCL `40` found:

| Address | Likely device |
| --- | --- |
| `0x20` | PCA9535 IO expander |
| `0x51` | PCF85063/PCF8563 RTC |
| `0x55` | BQ27220 battery gauge |
| `0x5D` | GT911 touch controller |
| `0x68` | TPS65185/e-paper power device, seen in an earlier scan but not consistently present |
| `0x6B` | BQ25896 charger |

## Touch bring-up notes

- Working touch driver: `SensorLib` `TouchDrvGT911`.
- Pins: SDA `39`, SCL `40`, IRQ `3`, RST `9`.
- Address: `0x5D`.
- Portrait touch-test page targets produced raw coordinates near the drawn target coordinates:
  - TL target: about `(76,293)`
  - TR target: about `(458,303)`
  - BL target: about `(84,727)`
  - BR target: about `(450,732)`
- Current mapping: GT911 raw coordinates match FastEPD portrait logical coordinates with rotation `90`; clamp to `540x960`.
- Initial polling produced repeated down/up events for a single physical touch; firmware now uses direct `getPoint()` reads plus release debounce.
- Debounced gesture checks passed on-device: taps, long-press, horizontal swipes left/right, and vertical swipe diagnostics are detected as single gestures.
- The touch-test page is gated behind `REMOTE_ENABLE_TOUCH_TEST`; normal firmware renders the status page and still initializes touch.

## Display bring-up notes

- Working display library: `bitbank2/FastEPD` using `BB_PANEL_EPDIY_V7` at 960×540.
- The older `Xinyuan-LILYGO/LilyGo-EPD47` driver path initialized and returned success over serial, but did not visibly refresh this Pro/Lite panel.
- First visible full-screen status page render succeeded on 2026-05-08.
- Measured FastEPD full refresh call time: about `754 ms`, result `0`.
- Visual orientation confirmed on device: portrait mode with charging/USB port down, FastEPD rotation `90`.
- Black/white visual check: text readable, contrast OK, no artifacts observed.
- Status summary text should be wrapped/clipped to stay on screen in portrait layout.
- 4-bit grayscale test: technically renders, but the visible levels are not monotonic/reliable enough for core UI. User observed the lowest dark levels are compressed/noisy, raw level `6` is anomalously dark, and the candidate palette looks like two separate scales. Treat grayscale as optional decorative/detail only; prefer black/white plus a small tested gray subset.

## Important next bring-up tasks

1. Confirm Lite board exact revision and silk-screen labels.
2. Run vendor display example before writing custom rendering code.
3. Run vendor touch example and record raw coordinate orientation.
4. Confirm touch wake/deep-sleep wiring before relying on battery behavior.
5. Measure real current draw in:
   - active WiFi
   - idle screen on / WiFi off
   - deep sleep
   - MagSafe charging
