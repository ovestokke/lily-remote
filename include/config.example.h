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

// Set to 1 only while testing safe write calls against the dummy helpers below.
#define REMOTE_ENABLE_HA_WRITE_TEST 0
#define HA_WRITE_TEST_ENTITY_ID "input_boolean.lily_remote_test"
#define HA_WRITE_TEST_TEXT_ENTITY_ID "input_text.lily_remote_last_test"

// Optional power telemetry helpers. These are safe write-only Home Assistant helpers
// used as a battery/sleep flight recorder when the remote wakes or sleeps.
#define REMOTE_ENABLE_POWER_TELEMETRY 1
#define HA_POWER_EVENT_ENTITY_ID "input_text.lily_remote_power_event"
#define HA_POWER_LOG_ENTITY_ID "input_text.lily_remote_power_log"
#define HA_POWER_VOLTAGE_ENTITY_ID "input_number.lily_remote_battery_voltage_mv"
#define HA_POWER_SOC_ENTITY_ID "input_number.lily_remote_raw_soc"
#define HA_POWER_WAKE_COUNT_ENTITY_ID "input_number.lily_remote_wake_count"

// Set to 1 only while visually testing 4-bit e-paper grayscale output.
#define REMOTE_ENABLE_GRAYSCALE_TEST 0

// Set to 1 only while calibrating touch coordinates/gestures.
#define REMOTE_ENABLE_TOUCH_TEST 0

// Set to 1 to boot into the safe dummy-helper control page.
#define REMOTE_ENABLE_SAFE_CONTROL_PAGE 1

// Set to 1 only while enumerating I2C devices during hardware bring-up.
#define REMOTE_ENABLE_I2C_SCAN 0

// Set to 1 later when deep-sleep behavior is implemented/tested.
#define REMOTE_SLEEP_AFTER_BOOT 0

// Normal UI idle sleep. E-paper keeps the last screen visible, so shorter idle
// sleep saves battery without blanking the UI. Production sleep is touch wake
// only; timer wake is audit-only and not a usable remote UX.
#define REMOTE_IDLE_SLEEP_ENABLED 1
#define REMOTE_IDLE_SLEEP_TIMEOUT_MS (90UL * 1000UL)
#define REMOTE_SLEEP_WAKE_TOUCH 1
#define REMOTE_SLEEP_WAKE_POLICY REMOTE_SLEEP_WAKE_TOUCH

// GT911 interrupt wake configuration for production touch sleep.
#define REMOTE_TOUCH_WAKE_GPIO 3
#define REMOTE_TOUCH_WAKE_LEVEL 0
#define REMOTE_WAIT_FOR_TOUCH_IRQ_RELEASE_MS 1500
#define REMOTE_TOUCH_SLEEP_CLEAR_TIMEOUT_MS 1500
#define REMOTE_TOUCH_SLEEP_RELEASE_I2C 0
#define REMOTE_SLEEP_RECOVERY_HOLD_MS (5UL * 60UL * 1000UL)
#define REMOTE_EXT0_FAST_WAKE_MS (10UL * 1000UL)
#define REMOTE_EXT0_WAKE_LOOP_LIMIT 3

// Power-audit mode for isolating battery drain.
// 0 = normal firmware
// 1 = timer-only sleep audit
// 2 = touch wake + timer heartbeat audit
// 3 = stay awake and log periodically
//
// Audit profile isolates sleep-drain suspects in timer-only mode.
// 0 = baseline cleanup
// 1 = extra ESP radio/RTC-peripheral cleanup
// 2 = put GT911 touch controller to sleep
// 3 = force e-paper power off/deinit before sleep
// 4 = release I2C pins before sleep
// 5 = full cleanup: profiles 1-4 together
// 99 = auto-cycle profiles 0-5 every REMOTE_POWER_AUDIT_PROFILE_CYCLES wakes
// Safety: audit modes stay awake while USB/external power is present. A cold
// boot/reset also gets a temporary recovery hold if external power is not
// detected, so a bad sleep test still has an upload window.
#define REMOTE_POWER_AUDIT_MODE 0
#define REMOTE_POWER_AUDIT_TIMER_MINUTES 30
#define REMOTE_POWER_AUDIT_PROFILE 0
#define REMOTE_POWER_AUDIT_PROFILE_CYCLES 3
#define REMOTE_POWER_AUDIT_ACTIVE_LOG_SECONDS 60
#define REMOTE_POWER_AUDIT_RESET_HOLD_SECONDS (5UL * 60UL)
#define REMOTE_POWER_AUDIT_UNPLUG_DEBOUNCE_SECONDS 5
