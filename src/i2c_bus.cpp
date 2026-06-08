#include "i2c_bus.h"

#include <Wire.h>

namespace {
bool g_i2cInitialized = false;
}

void initRemoteI2cBus() {
  if (g_i2cInitialized) {
    return;
  }

  Wire.begin(kRemoteI2cSda, kRemoteI2cScl);
  g_i2cInitialized = true;
}

void shutdownRemoteI2cBusForSleep() {
  if (g_i2cInitialized) {
    Wire.end();
    g_i2cInitialized = false;
  }

  pinMode(kRemoteI2cSda, INPUT);
  pinMode(kRemoteI2cScl, INPUT);
}
