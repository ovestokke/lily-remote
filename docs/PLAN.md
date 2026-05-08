# E-Ink Smart Home Remote Plan

**Dato:** 2026-04-08  
**Status:** Hardware bestilt  
**Konsept:** Håndholdt e-ink fjernkontroll for styring av hele stua via Home Assistant.

## Hardware

Valgt hardware: **LILYGO T5 E-Paper S3 Pro Lite** — Lite-varianten uten LoRa/GPS.

| Egenskap | Verdi |
| --- | --- |
| Skjerm | 4.7" e-ink, 960×540, 16 gråtoner |
| Touch | Kapasitiv, 2-punkt |
| Prosessor | ESP32-S3-WROOM-1, dual-core 240 MHz |
| RAM / Flash | 8 MB PSRAM / 16 MB flash |
| Batteri | Eget batteri via JST |
| MicroSD | Ja |
| RTC | PCF8563 / PCF85063-familien |
| Trådløs lading | Ja, MagSafe-kompatibel |

## Devices controlled through Home Assistant

| Enhet | HA-integrasjon | Funksjoner |
| --- | --- | --- |
| Home Assistant | REST API / WebSocket | Hub for alt |
| KEF LS60 | KEF-integrasjon | Volum, input, play/pause, EQ |
| LG TV | WebOS-integrasjon | Av/på, input, volum, apps |
| WiiM Ultra | UPnP/DLNA / HA-integrasjon | Play/pause, kilde, volum |
| Philips Hue | Native HA-integrasjon | Lys, scener, dimming, fargetemperatur |

## Architecture

```text
LilyGo T5 S3 Pro Lite --WiFi--> Home Assistant REST/WebSocket API --> Alle enheter
```

Rules:

- Remote talks only to Home Assistant.
- Home Assistant handles WebOS, KEF, Hue Bridge, WiiM, etc.
- Auth via Home Assistant long-lived access token.
- WebSocket can be added for real-time state updates once REST bring-up works.

## UI concept

Swipe between pages:

1. **Stue**
   - LG TV power, input selection, app shortcuts.
   - Volume control mapped to TV or KEF depending on context.
2. **Musikk**
   - Now playing, transport buttons, KEF/WiiM volume, source selection.
3. **Lys**
   - Room scenes, group dimming, presets: Film, Middag, Kveld, Av.
4. **Klima / Info**
   - Indoor/outdoor temperature, heating status, other HA status.

## E-ink UI constraints

- Prefer buttons/icons and +/- controls over sliders.
- Use full refresh on page changes.
- Use partial refresh for small changing regions.
- High-contrast black/white first; add 16-grayscale icon/detail work later.
- Avoid rapid animations; design for deliberate remote-control interactions.

## Technical direction

- PlatformIO + Arduino framework + C++.
- REST API first for simple service calls.
- WebSocket later for live state updates.
- Touch driver bring-up before complex UI.
- Deep sleep/wake strategy after controls are reliable.

## External inspiration

- ugomeda/home-assistant-epaper-remote
- mcfedr/home-assistant-epaper-remote
- M5Hamote
- ChrIsSmart DIY Remote
- CybDis LilyGo T5 Home Assistant Dashboard
- T5 Informer
- Brian Dorey Home Assistant E-Ink Display
- LVGL ESP32 E-Paper port
