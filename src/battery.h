#pragma once

#include <Arduino.h>

enum class RemoteChargeStatus : uint8_t {
  Unknown,
  NotCharging,
  PreCharge,
  FastCharging,
  ChargeDone,
};

struct RemoteBatteryReading {
  bool available = false;
  uint8_t percent = 0;
  bool voltageKnown = false;
  uint16_t voltageMv = 0;
  bool currentKnown = false;
  int16_t currentMa = 0;
  bool averageCurrentKnown = false;
  int16_t averageCurrentMa = 0;
  bool remainingCapacityKnown = false;
  uint16_t remainingCapacityMah = 0;
  bool fullChargeCapacityKnown = false;
  uint16_t fullChargeCapacityMah = 0;
  bool stateOfHealthKnown = false;
  uint8_t stateOfHealthPercent = 0;
  bool batteryStatusKnown = false;
  uint16_t batteryStatus = 0;
  bool operationStatusKnown = false;
  uint16_t operationStatus = 0;

  bool chargerAvailable = false;
  bool externalPower = false;
  RemoteChargeStatus chargeStatus = RemoteChargeStatus::Unknown;
  uint8_t vbusStatus = 0;
  uint8_t rawChargerStatus = 0;
  bool chargerFaultKnown = false;
  uint8_t rawChargerFault = 0;
  bool chargeCurrentLimitKnown = false;
  uint16_t chargeCurrentLimitMa = 0;
  bool chargeVoltageLimitKnown = false;
  uint16_t chargeVoltageLimitMv = 0;
  bool terminationCurrentKnown = false;
  uint16_t terminationCurrentMa = 0;
};

bool configureRemoteChargerForRemoteUse();
bool readRemoteBattery(RemoteBatteryReading &reading);
const char *chargeStatusName(RemoteChargeStatus status);
