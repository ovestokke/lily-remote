#include "battery.h"

#include "i2c_bus.h"

#include <Wire.h>

namespace {
constexpr uint8_t kBq27220Address = 0x55;
constexpr uint8_t kStateOfChargeRegister = 0x2C;
constexpr uint16_t kMaxReasonablePercent = 100;

bool readWord(uint8_t reg, uint16_t &value) {
  initRemoteI2cBus();

  Wire.beginTransmission(kBq27220Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t bytesRead = Wire.requestFrom(kBq27220Address, static_cast<uint8_t>(2));
  if (bytesRead != 2) {
    return false;
  }

  const uint8_t lsb = Wire.read();
  const uint8_t msb = Wire.read();
  value = static_cast<uint16_t>(lsb) | (static_cast<uint16_t>(msb) << 8);
  return true;
}
} // namespace

bool readRemoteBattery(RemoteBatteryReading &reading) {
  reading = RemoteBatteryReading{};

  uint16_t rawPercent = 0;
  if (!readWord(kStateOfChargeRegister, rawPercent)) {
    return false;
  }

  if (rawPercent > kMaxReasonablePercent) {
    return false;
  }

  reading.available = true;
  reading.percent = static_cast<uint8_t>(rawPercent);
  return true;
}
