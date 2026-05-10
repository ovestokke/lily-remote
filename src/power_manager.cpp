#include "power_manager.h"

#include "display.h"
#include "log.h"
#include <driver/gpio.h>

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
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleepAfterBootSeconds_) * 1000000ULL);
  esp_deep_sleep_start();
}

void PowerManager::goToSleep() {
  logPrintf(LogLevel::Info, "Entering deep sleep indefinitely (wake on touch)...");
  Serial.flush();
  
  renderSleepPage();
  
  // Enable wake up from GT911 touch interrupt (GPIO 3, active LOW)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_3, 0);
  
  esp_deep_sleep_start();
}
