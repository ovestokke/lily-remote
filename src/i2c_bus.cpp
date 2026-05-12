#include "i2c_bus.h"

#include <Wire.h>

void initRemoteI2cBus() {
  static bool initialized = false;
  if (initialized) {
    return;
  }

  Wire.begin(kRemoteI2cSda, kRemoteI2cScl);
  initialized = true;
}
