#include "battery.h"

#include "i2c_bus.h"

#include <Wire.h>

namespace {
constexpr uint8_t kBq27220Address = 0x55;
constexpr uint8_t kBq25896Address = 0x6B;

constexpr uint8_t kBatteryStatusRegister = 0x0A;
constexpr uint8_t kCurrentRegister = 0x0C;
constexpr uint8_t kVoltageRegister = 0x08;
constexpr uint8_t kRemainingCapacityRegister = 0x10;
constexpr uint8_t kFullChargeCapacityRegister = 0x12;
constexpr uint8_t kAverageCurrentRegister = 0x14;
constexpr uint8_t kStateOfChargeRegister = 0x2C;
constexpr uint8_t kStateOfHealthRegister = 0x2E;
constexpr uint8_t kOperationStatusRegister = 0x3A;

constexpr uint8_t kChargerCurrentLimitRegister = 0x04;
constexpr uint8_t kChargerPrechargeTerminationRegister = 0x05;
constexpr uint8_t kChargerVoltageLimitRegister = 0x06;
constexpr uint8_t kChargerTimerControlRegister = 0x07;
constexpr uint8_t kChargerStatusRegister = 0x0B;
constexpr uint8_t kChargerFaultRegister = 0x0C;
constexpr uint16_t kMaxReasonablePercent = 100;

bool readByte(uint8_t address, uint8_t reg, uint8_t &value) {
  initRemoteI2cBus();

  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t bytesRead = Wire.requestFrom(address, static_cast<uint8_t>(1));
  if (bytesRead != 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

bool readWord(uint8_t address, uint8_t reg, uint16_t &value) {
  initRemoteI2cBus();

  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t bytesRead = Wire.requestFrom(address, static_cast<uint8_t>(2));
  if (bytesRead != 2) {
    return false;
  }

  const uint8_t lsb = Wire.read();
  const uint8_t msb = Wire.read();
  value = static_cast<uint16_t>(lsb) | (static_cast<uint16_t>(msb) << 8);
  return true;
}

bool writeByte(uint8_t address, uint8_t reg, uint8_t value) {
  initRemoteI2cBus();

  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

RemoteChargeStatus parseChargeStatus(uint8_t reg0b) {
  switch ((reg0b >> 3) & 0x03) {
  case 0x01:
    return RemoteChargeStatus::PreCharge;
  case 0x02:
    return RemoteChargeStatus::FastCharging;
  case 0x03:
    return RemoteChargeStatus::ChargeDone;
  case 0x00:
  default:
    return RemoteChargeStatus::NotCharging;
  }
}

uint16_t decodeChargerCurrentLimitMa(uint8_t reg04) {
  return static_cast<uint16_t>(reg04 & 0x7F) * 64U;
}

uint16_t decodeChargerVoltageLimitMv(uint8_t reg06) {
  return 3840U + static_cast<uint16_t>((reg06 >> 2) & 0x3F) * 16U;
}

uint16_t decodeTerminationCurrentMa(uint8_t reg05) {
  return 64U + static_cast<uint16_t>(reg05 & 0x0F) * 64U;
}

} // namespace

bool configureRemoteChargerForRemoteUse() {
  uint8_t timerControl = 0;
  if (!readByte(kBq25896Address, kChargerTimerControlRegister, timerControl)) {
    return false;
  }

  // The BQ25896 defaults to an I2C watchdog. This firmware is not a continuous
  // charger supervisor, so disable only the I2C watchdog and preserve charge
  // termination and safety-timer settings.
  constexpr uint8_t kWatchdogBits = 0x30;
  if ((timerControl & kWatchdogBits) == 0) {
    return true;
  }

  return writeByte(kBq25896Address, kChargerTimerControlRegister, timerControl & ~kWatchdogBits);
}

bool readRemoteBattery(RemoteBatteryReading &reading) {
  reading = RemoteBatteryReading{};

  uint16_t rawPercent = 0;
  if (readWord(kBq27220Address, kStateOfChargeRegister, rawPercent) && rawPercent <= kMaxReasonablePercent) {
    reading.available = true;
    reading.percent = static_cast<uint8_t>(rawPercent);
  }

  uint16_t voltageMv = 0;
  if (readWord(kBq27220Address, kVoltageRegister, voltageMv)) {
    reading.voltageKnown = true;
    reading.voltageMv = voltageMv;
  }

  uint16_t currentRaw = 0;
  if (readWord(kBq27220Address, kCurrentRegister, currentRaw)) {
    reading.currentKnown = true;
    reading.currentMa = static_cast<int16_t>(currentRaw);
  }

  uint16_t averageCurrentRaw = 0;
  if (readWord(kBq27220Address, kAverageCurrentRegister, averageCurrentRaw)) {
    reading.averageCurrentKnown = true;
    reading.averageCurrentMa = static_cast<int16_t>(averageCurrentRaw);
  }

  uint16_t remainingCapacityMah = 0;
  if (readWord(kBq27220Address, kRemainingCapacityRegister, remainingCapacityMah)) {
    reading.remainingCapacityKnown = true;
    reading.remainingCapacityMah = remainingCapacityMah;
  }

  uint16_t fullChargeCapacityMah = 0;
  if (readWord(kBq27220Address, kFullChargeCapacityRegister, fullChargeCapacityMah)) {
    reading.fullChargeCapacityKnown = true;
    reading.fullChargeCapacityMah = fullChargeCapacityMah;
  }

  uint16_t stateOfHealthRaw = 0;
  if (readWord(kBq27220Address, kStateOfHealthRegister, stateOfHealthRaw) && (stateOfHealthRaw & 0x00FF) <= 100) {
    reading.stateOfHealthKnown = true;
    reading.stateOfHealthPercent = static_cast<uint8_t>(stateOfHealthRaw & 0x00FF);
  }

  uint16_t batteryStatus = 0;
  if (readWord(kBq27220Address, kBatteryStatusRegister, batteryStatus)) {
    reading.batteryStatusKnown = true;
    reading.batteryStatus = batteryStatus;
  }

  uint16_t operationStatus = 0;
  if (readWord(kBq27220Address, kOperationStatusRegister, operationStatus)) {
    reading.operationStatusKnown = true;
    reading.operationStatus = operationStatus;
  }

  uint8_t chargerStatus = 0;
  if (readByte(kBq25896Address, kChargerStatusRegister, chargerStatus)) {
    reading.chargerAvailable = true;
    reading.rawChargerStatus = chargerStatus;
    reading.vbusStatus = (chargerStatus >> 5) & 0x07;
    reading.externalPower = (chargerStatus & 0x04) != 0;
    reading.chargeStatus = parseChargeStatus(chargerStatus);
  }

  uint8_t chargerFault = 0;
  if (readByte(kBq25896Address, kChargerFaultRegister, chargerFault)) {
    reading.chargerFaultKnown = true;
    reading.rawChargerFault = chargerFault;
  }

  uint8_t chargerCurrentLimit = 0;
  if (readByte(kBq25896Address, kChargerCurrentLimitRegister, chargerCurrentLimit)) {
    reading.chargeCurrentLimitKnown = true;
    reading.chargeCurrentLimitMa = decodeChargerCurrentLimitMa(chargerCurrentLimit);
  }

  uint8_t chargerVoltageLimit = 0;
  if (readByte(kBq25896Address, kChargerVoltageLimitRegister, chargerVoltageLimit)) {
    reading.chargeVoltageLimitKnown = true;
    reading.chargeVoltageLimitMv = decodeChargerVoltageLimitMv(chargerVoltageLimit);
  }

  uint8_t chargerTermination = 0;
  if (readByte(kBq25896Address, kChargerPrechargeTerminationRegister, chargerTermination)) {
    reading.terminationCurrentKnown = true;
    reading.terminationCurrentMa = decodeTerminationCurrentMa(chargerTermination);
  }

  return reading.available || reading.chargerAvailable || reading.voltageKnown || reading.averageCurrentKnown;
}

const char *chargeStatusName(RemoteChargeStatus status) {
  switch (status) {
  case RemoteChargeStatus::NotCharging:
    return "not charging";
  case RemoteChargeStatus::PreCharge:
    return "pre-charge";
  case RemoteChargeStatus::FastCharging:
    return "fast charging";
  case RemoteChargeStatus::ChargeDone:
    return "charge done";
  case RemoteChargeStatus::Unknown:
  default:
    return "unknown";
  }
}
