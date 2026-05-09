#include "power_manager.h"

#include "log.h"

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
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleepAfterBootSeconds_) * 1000000ULL);
  esp_deep_sleep_start();
}
