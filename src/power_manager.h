#pragma once

#include <Arduino.h>

constexpr uint8_t kPowerAuditProfileBaseline = 0;
constexpr uint8_t kPowerAuditProfileEspCleanup = 1;
constexpr uint8_t kPowerAuditProfileTouchSleep = 2;
constexpr uint8_t kPowerAuditProfileEpdPowerOff = 3;
constexpr uint8_t kPowerAuditProfileI2cHighZ = 4;
constexpr uint8_t kPowerAuditProfileFullCleanup = 5;
constexpr uint8_t kPowerAuditProfileAuto = 99;

class PowerManager {
public:
  explicit PowerManager(uint32_t sleepAfterBootSeconds = 60);

  void maybeSleepAfterBoot(bool enabled);
  bool prepareTouchSleep();
  bool finishTouchSleep();
  bool enterTouchSleep(const char *reason);
  void goToSleep();
  void auditSleep(uint32_t timerSeconds, bool enableTouchWake, uint8_t auditProfile = kPowerAuditProfileBaseline);

private:
  uint32_t sleepAfterBootSeconds_;
};
