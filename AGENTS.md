# AGENTS.md

## Project

Firmware for a LILYGO T5 E-Paper S3 Pro Lite Home Assistant remote.

## Commands

```bash
pio run                 # build
pio run -t upload       # flash connected board
pio device monitor      # serial monitor at 115200
```

## Secret handling

- Never commit WiFi passwords, Home Assistant tokens, or local IP secrets.
- Local firmware config lives in `include/config.h` and is gitignored.
- Use `include/config.example.h` for documented placeholders only.
- Ask before reading or modifying files likely to contain credentials.

## Firmware principles

- Remote talks only to Home Assistant, not directly to living-room devices.
- Bring-up should stay read-only until basic WiFi/HA connectivity is verified.
- Favor slow, reliable e-ink interactions over animations.
- Prefer full refresh on page transitions and partial refresh for small changing regions.
- Keep UI code modular: pages, controls, HA API client, display driver, touch driver, power manager.

## Hardware assumptions to verify on-device

- Board: LILYGO T5 E-Paper S3 Pro Lite, ESP32-S3, 16 MB flash, 8 MB PSRAM.
- Display: ED047TC1 960×540.
- Touch: GT911.
- RTC/power chips may vary by revision; verify with I2C scan before depending on exact addresses.
