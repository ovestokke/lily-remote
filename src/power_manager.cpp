#include "power_manager.h"

#include "display.h"
#include "log.h"

#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

#ifndef REMOTE_TOUCH_WAKE_GPIO
#define REMOTE_TOUCH_WAKE_GPIO 3
#endif
#ifndef REMOTE_TOUCH_WAKE_LEVEL
#define REMOTE_TOUCH_WAKE_LEVEL 0
#endif
#ifndef REMOTE_WAIT_FOR_TOUCH_IRQ_RELEASE_MS
#define REMOTE_WAIT_FOR_TOUCH_IRQ_RELEASE_MS 1500
#endif
#ifndef REMOTE_SLEEP_FALLBACK_WAKE_SECONDS
#define REMOTE_SLEEP_FALLBACK_WAKE_SECONDS (15UL * 60UL)
#endif

namespace {
void shutdownWifiForSleep() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool waitForTouchWakeInactive() {
  const gpio_num_t touchWakeGpio = static_cast<gpio_num_t>(REMOTE_TOUCH_WAKE_GPIO);
  pinMode(REMOTE_TOUCH_WAKE_GPIO, INPUT_PULLUP);

  const uint32_t start = millis();
  while (gpio_get_level(touchWakeGpio) == REMOTE_TOUCH_WAKE_LEVEL &&
         millis() - start < REMOTE_WAIT_FOR_TOUCH_IRQ_RELEASE_MS) {
    delay(20);
  }

  return gpio_get_level(touchWakeGpio) != REMOTE_TOUCH_WAKE_LEVEL;
}

void armTimerWake(uint32_t seconds) {
  const uint32_t safeSeconds = seconds > 0 ? seconds : 60;
  const esp_err_t err = esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(safeSeconds) * 1000000ULL);
  if (err != ESP_OK) {
    logPrintf(LogLevel::Error,
              "Failed to arm timer wake for %u seconds: %d",
              static_cast<unsigned>(safeSeconds),
              static_cast<int>(err));
  }
}

void enableTouchWakeOrFallbackTimer() {
  const gpio_num_t touchWakeGpio = static_cast<gpio_num_t>(REMOTE_TOUCH_WAKE_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  if (!waitForTouchWakeInactive()) {
    logPrintf(LogLevel::Warn,
              "Touch wake GPIO %d is still active; using %u second timer wake to avoid an immediate wake loop",
              REMOTE_TOUCH_WAKE_GPIO,
              static_cast<unsigned>(REMOTE_SLEEP_FALLBACK_WAKE_SECONDS));
    armTimerWake(REMOTE_SLEEP_FALLBACK_WAKE_SECONDS);
    return;
  }

  const esp_err_t err = esp_sleep_enable_ext0_wakeup(touchWakeGpio, REMOTE_TOUCH_WAKE_LEVEL);
  if (err == ESP_OK) {
    logPrintf(LogLevel::Info,
              "Touch wake armed on GPIO %d level %d",
              REMOTE_TOUCH_WAKE_GPIO,
              REMOTE_TOUCH_WAKE_LEVEL);
    return;
  }

  logPrintf(LogLevel::Error,
            "Failed to arm touch wake on GPIO %d: %d; using %u second timer fallback",
            REMOTE_TOUCH_WAKE_GPIO,
            static_cast<int>(err),
            static_cast<unsigned>(REMOTE_SLEEP_FALLBACK_WAKE_SECONDS));
  armTimerWake(REMOTE_SLEEP_FALLBACK_WAKE_SECONDS);
}
} // namespace

PowerManager::PowerManager(uint32_t sleepAfterBootSeconds)
    : sleepAfterBootSeconds_(sleepAfterBootSeconds) {}

void PowerManager::maybeSleepAfterBoot(bool enabled) {
  if (!enabled) {
    return;
  }

  logPrintf(LogLevel::Info,
            "Sleeping for %u seconds...",
            static_cast<unsigned>(sleepAfterBootSeconds_));
  Serial.flush();
  renderSleepPage();
  shutdownWifiForSleep();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  armTimerWake(sleepAfterBootSeconds_);
  esp_deep_sleep_start();
}

void PowerManager::goToSleep() {
  logPrintf(LogLevel::Info, "Entering deep sleep indefinitely (wake on touch)...");
  Serial.flush();

  renderSleepPage();
  shutdownWifiForSleep();
  enableTouchWakeOrFallbackTimer();
  esp_deep_sleep_start();
}

void PowerManager::auditSleep(uint32_t timerSeconds, bool enableTouchWake) {
  const uint32_t safeTimerSeconds = timerSeconds > 0 ? timerSeconds : 60;
  logPrintf(LogLevel::Info,
            "Entering audit deep sleep for %u seconds%s",
            static_cast<unsigned>(safeTimerSeconds),
            enableTouchWake ? " with touch wake" : "");
  Serial.flush();

  shutdownWifiForSleep();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  armTimerWake(safeTimerSeconds);

  if (enableTouchWake) {
    const gpio_num_t touchWakeGpio = static_cast<gpio_num_t>(REMOTE_TOUCH_WAKE_GPIO);
    if (waitForTouchWakeInactive()) {
      const esp_err_t err = esp_sleep_enable_ext0_wakeup(touchWakeGpio, REMOTE_TOUCH_WAKE_LEVEL);
      if (err == ESP_OK) {
        logPrintf(LogLevel::Info,
                  "Audit touch wake armed on GPIO %d level %d with timer heartbeat",
                  REMOTE_TOUCH_WAKE_GPIO,
                  REMOTE_TOUCH_WAKE_LEVEL);
      } else {
        logPrintf(LogLevel::Error,
                  "Failed to arm audit touch wake on GPIO %d: %d; timer heartbeat remains armed",
                  REMOTE_TOUCH_WAKE_GPIO,
                  static_cast<int>(err));
      }
    } else {
      logPrintf(LogLevel::Warn,
                "Touch wake GPIO %d is still active; audit sleep will use timer heartbeat only",
                REMOTE_TOUCH_WAKE_GPIO);
    }
  }

  esp_deep_sleep_start();
}
