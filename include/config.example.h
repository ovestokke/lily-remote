#pragma once

// Copy this file to include/config.h and fill in your local values.
// include/config.h is gitignored. Do not commit HA tokens or WiFi passwords.

#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// No trailing slash. Examples:
//   "http://homeassistant.local:8123"
//   "http://192.168.1.10:8123"
#define HA_BASE_URL "http://homeassistant.local:8123"

// Home Assistant profile -> Security -> Long-lived access tokens.
#define HA_TOKEN "paste-long-lived-access-token-here"

// Safe read-only entity used by the first firmware prototype.
// Change to something you know exists, e.g. "light.stue" or "sensor.living_room_temperature".
#define HA_TEST_ENTITY_ID "sensor.time"

// Set to 1 later when deep-sleep behavior is implemented/tested.
#define REMOTE_SLEEP_AFTER_BOOT 0
