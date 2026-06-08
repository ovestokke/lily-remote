#include "power_manager.h"

#include "display.h"
#include "i2c_bus.h"
#include "log.h"
#include "touch.h"

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
#ifndef REMOTE_TOUCH_SLEEP_CLEAR_TIMEOUT_MS
#define REMOTE_TOUCH_SLEEP_CLEAR_TIMEOUT_MS 1500
#endif
#ifndef REMOTE_TOUCH_SLEEP_RELEASE_I2C
#define REMOTE_TOUCH_SLEEP_RELEASE_I2C 0
#endif

namespace {
void shutdownWifiForSleep() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void shutdownEspPeripheralsForSleep() {
  shutdownWifiForSleep();
#if defined(CONFIG_BT_ENABLED)
  btStop();
#endif
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
}

void applyAuditProfileForSleep(uint8_t auditProfile, bool enableTouchWake) {
  switch (auditProfile) {
  case kPowerAuditProfileEspCleanup:
    shutdownEspPeripheralsForSleep();
    break;
  case kPowerAuditProfileTouchSleep:
    if (!enableTouchWake) {
      const bool ok = sleepRemoteTouchControllerForPowerAudit();
      logPrintf(ok ? LogLevel::Info : LogLevel::Warn,
                "GT911 audit sleep command %s",
                ok ? "sent" : "failed");
    }
    break;
  case kPowerAuditProfileEpdPowerOff:
    shutdownRemoteDisplayForSleep();
    break;
  case kPowerAuditProfileI2cHighZ:
    shutdownRemoteI2cBusForSleep();
    break;
  case kPowerAuditProfileFullCleanup:
    shutdownEspPeripheralsForSleep();
    if (!enableTouchWake) {
      const bool ok = sleepRemoteTouchControllerForPowerAudit();
      logPrintf(ok ? LogLevel::Info : LogLevel::Warn,
                "GT911 audit sleep command %s",
                ok ? "sent" : "failed");
    }
    shutdownRemoteDisplayForSleep();
    shutdownRemoteI2cBusForSleep();
    break;
  case kPowerAuditProfileBaseline:
  default:
    break;
  }
}

bool waitForTouchWakeInactive(uint32_t timeoutMs = REMOTE_WAIT_FOR_TOUCH_IRQ_RELEASE_MS) {
  const gpio_num_t touchWakeGpio = static_cast<gpio_num_t>(REMOTE_TOUCH_WAKE_GPIO);
  pinMode(REMOTE_TOUCH_WAKE_GPIO, INPUT_PULLUP);

  const uint32_t start = millis();
  while (gpio_get_level(touchWakeGpio) == REMOTE_TOUCH_WAKE_LEVEL &&
         millis() - start < timeoutMs) {
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

bool armTouchWakeOnly() {
  const gpio_num_t touchWakeGpio = static_cast<gpio_num_t>(REMOTE_TOUCH_WAKE_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  const esp_err_t err = esp_sleep_enable_ext0_wakeup(touchWakeGpio, REMOTE_TOUCH_WAKE_LEVEL);
  if (err != ESP_OK) {
    logPrintf(LogLevel::Error,
              "Failed to arm touch wake on GPIO %d: %d",
              REMOTE_TOUCH_WAKE_GPIO,
              static_cast<int>(err));
    return false;
  }

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
  logPrintf(LogLevel::Info,
            "Touch wake armed on GPIO %d level %d",
            REMOTE_TOUCH_WAKE_GPIO,
            REMOTE_TOUCH_WAKE_LEVEL);
  return true;
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

bool PowerManager::prepareTouchSleep() {
  if (!clearRemoteTouchForSleep(REMOTE_TOUCH_SLEEP_CLEAR_TIMEOUT_MS)) {
    logPrintf(LogLevel::Warn, "Touch sleep preflight failed: touch/IRQ did not clear");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    return false;
  }

  if (!waitForTouchWakeInactive()) {
    logPrintf(LogLevel::Warn, "Touch sleep preflight failed: IRQ still active");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    return false;
  }

  return armTouchWakeOnly();
}

bool PowerManager::finishTouchSleep() {
  if (!waitForTouchWakeInactive()) {
    logPrintf(LogLevel::Warn, "Touch IRQ became active before sleep commit; aborting sleep");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    return false;
  }

  logPrintf(LogLevel::Info, "Entering production touch deep sleep");
  Serial.flush();

  renderSleepPage();
  if (!clearRemoteTouchForSleep(REMOTE_TOUCH_SLEEP_CLEAR_TIMEOUT_MS) || !waitForTouchWakeInactive()) {
    logPrintf(LogLevel::Warn, "Touch became active during sleep-page render; aborting sleep");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    return false;
  }

  shutdownWifiForSleep();
#if defined(CONFIG_BT_ENABLED)
  btStop();
#endif
  shutdownRemoteDisplayForSleep();
#if REMOTE_TOUCH_SLEEP_RELEASE_I2C
  shutdownRemoteI2cBusForSleep();
#endif

  Serial.flush();
  esp_deep_sleep_start();
  return true;
}

bool PowerManager::enterTouchSleep(const char *reason) {
  logPrintf(LogLevel::Info, "Touch sleep requested: %s", reason != nullptr ? reason : "unknown");
  if (!prepareTouchSleep()) {
    return false;
  }
  return finishTouchSleep();
}

void PowerManager::goToSleep() {
  enterTouchSleep("manual");
}

void PowerManager::auditSleep(uint32_t timerSeconds, bool enableTouchWake, uint8_t auditProfile) {
  const uint32_t safeTimerSeconds = timerSeconds > 0 ? timerSeconds : 60;
  logPrintf(LogLevel::Info,
            "Entering audit deep sleep for %u seconds%s profile=%u",
            static_cast<unsigned>(safeTimerSeconds),
            enableTouchWake ? " with touch wake" : "",
            static_cast<unsigned>(auditProfile));
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

  applyAuditProfileForSleep(auditProfile, enableTouchWake);
  Serial.flush();
  esp_deep_sleep_start();
}
