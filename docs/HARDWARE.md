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
| Touch | GT911, commonly at I2C `0x5D` |
| RTC | PCF85063/PCF8563 family |
| Battery/power | BQ25896 + BQ27220 family on Pro boards |
| IO expander | PCA9535 family |

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
