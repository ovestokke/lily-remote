#pragma once

#include <Arduino.h>

class PowerManager {
public:
  explicit PowerManager(uint32_t sleepAfterBootSeconds = 60);

  void maybeSleepAfterBoot(bool enabled);

private:
  uint32_t sleepAfterBootSeconds_;
};
