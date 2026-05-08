# E-Ink Smart Home Remote

Firmware repo for a handheld Home Assistant remote built on the **LILYGO T5 E-Paper S3 Pro Lite**.

- ESP32-S3-WROOM-1, 8 MB PSRAM, 16 MB flash
- 4.7" 960×540 e-paper, 16 grayscale levels
- Capacitive touch
- WiFi control through Home Assistant only
- Arduino/PlatformIO C++ firmware

## Architecture

```text
LILYGO T5 E-Paper S3 Pro Lite --WiFi--> Home Assistant API --> TV / KEF / WiiM / Hue / climate
```

The remote should not talk directly to living-room devices. Home Assistant is the integration hub.

## Current status

Initial repo scaffold + bring-up firmware:

- PlatformIO project for ESP32-S3
- Custom PlatformIO board file copied from LILYGO's `LilyGo-EPD47` ESP32-S3 branch
- WiFi connection test
- Read-only Home Assistant REST API test
- Secret config kept out of git

Display, touch, partial refresh, WebSocket status updates, and deep sleep are next milestones.

## Quick start

```bash
cd /home/ove/projects/lily-remote
cp include/config.example.h include/config.h
$EDITOR include/config.h
pio run
pio run -t upload
pio device monitor
```

If `pio` is not installed yet, install the PlatformIO Core CLI or use the VS Code PlatformIO extension recommended in `.vscode/extensions.json`.

`include/config.h` is ignored by git. Put WiFi credentials and the Home Assistant long-lived access token there.

## First firmware behavior

On boot, `src/main.cpp`:

1. Opens serial at `115200` baud.
2. Connects to WiFi.
3. Calls `GET /api/` on Home Assistant.
4. Calls `GET /api/states/<HA_TEST_ENTITY_ID>`.
5. Prints results to serial.

This is intentionally read-only for safe bring-up.

## Planned milestones

1. Hardware bring-up: serial, WiFi, HA REST ping.
2. E-paper display init and simple full-screen render.
3. Touch init and tap/swipe detection.
4. UI shell with pages: Stue, Musikk, Lys, Klima/Info.
5. HA service calls for lights/media/TV.
6. Partial refresh for small status/button regions.
7. Power work: WiFi duty cycling, touch wake, RTC/weather updates, deep sleep.

See [`docs/PLAN.md`](docs/PLAN.md) for the project concept.

Hardware issue notes:

- [`docs/LOOSE_INTERNAL_PART.md`](docs/LOOSE_INTERNAL_PART.md) — current loose/rattling internal part investigation and official-doc status.
