#pragma once

#include <Arduino.h>

class PowerManager {
public:
  explicit PowerManager(uint32_t sleepAfterBootSeconds = 60);

  void maybeSleepAfterBoot(bool enabled);
  void goToSleep();
  void auditSleep(uint32_t timerSeconds, bool enableTouchWake);

private:
  uint32_t sleepAfterBootSeconds_;
};
