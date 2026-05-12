#pragma once

#include <Arduino.h>

struct RemoteBatteryReading {
  bool available = false;
  uint8_t percent = 0;
};

bool readRemoteBattery(RemoteBatteryReading &reading);
