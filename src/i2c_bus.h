#pragma once

#include <Arduino.h>

constexpr int kRemoteI2cSda = 39;
constexpr int kRemoteI2cScl = 40;

void initRemoteI2cBus();
void shutdownRemoteI2cBusForSleep();
