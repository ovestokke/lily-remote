#include "app.h"

#include <Arduino.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "battery.h"
#include "display.h"
#include "ha_client.h"
#include "log.h"
#include "power_manager.h"
#include "touch.h"
#include "ui.h"
#include "version.h"

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

#if defined(REMOTE_BUILD_RELEASE) && REMOTE_POWER_AUDIT_MODE != 0
#error "Release builds must not enable REMOTE_POWER_AUDIT_MODE"
#endif

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint32_t kServiceCallCooldownMs = 1200;

#ifndef REMOTE_IDLE_SLEEP_ENABLED
#define REMOTE_IDLE_SLEEP_ENABLED 1
#endif
#ifndef REMOTE_IDLE_SLEEP_TIMEOUT_MS
#define REMOTE_IDLE_SLEEP_TIMEOUT_MS (90UL * 1000UL)
#endif
#ifndef REMOTE_SLEEP_WAKE_TOUCH
#define REMOTE_SLEEP_WAKE_TOUCH 1
#endif
#ifndef REMOTE_SLEEP_WAKE_POLICY
#define REMOTE_SLEEP_WAKE_POLICY REMOTE_SLEEP_WAKE_TOUCH
#endif
#ifndef REMOTE_TOUCH_WAKE_GPIO
#define REMOTE_TOUCH_WAKE_GPIO 3
#endif
#ifndef REMOTE_SLEEP_RECOVERY_HOLD_MS
#define REMOTE_SLEEP_RECOVERY_HOLD_MS (5UL * 60UL * 1000UL)
#endif
#ifndef REMOTE_EXT0_FAST_WAKE_MS
#define REMOTE_EXT0_FAST_WAKE_MS (10UL * 1000UL)
#endif
#ifndef REMOTE_EXT0_WAKE_LOOP_LIMIT
#define REMOTE_EXT0_WAKE_LOOP_LIMIT 3
#endif

#if REMOTE_SLEEP_WAKE_POLICY != REMOTE_SLEEP_WAKE_TOUCH
#error "Normal remote sleep must use touch wake"
#endif

constexpr uint32_t kIdleSleepTimeoutMs = REMOTE_IDLE_SLEEP_TIMEOUT_MS;

#ifndef REMOTE_ENABLE_HA_WRITE_TEST
#define REMOTE_ENABLE_HA_WRITE_TEST 0
#endif

#ifndef HA_WRITE_TEST_ENTITY_ID
#define HA_WRITE_TEST_ENTITY_ID "input_boolean.lily_remote_test"
#endif

#ifndef HA_WRITE_TEST_TEXT_ENTITY_ID
#define HA_WRITE_TEST_TEXT_ENTITY_ID "input_text.lily_remote_last_test"
#endif

#ifndef REMOTE_ENABLE_TOUCH_TEST
#define REMOTE_ENABLE_TOUCH_TEST 0
#endif

#ifndef REMOTE_ENABLE_SAFE_CONTROL_PAGE
#define REMOTE_ENABLE_SAFE_CONTROL_PAGE 1
#endif

#ifndef REMOTE_SLEEP_AFTER_BOOT
#define REMOTE_SLEEP_AFTER_BOOT 0
#endif

#ifndef REMOTE_ENABLE_POWER_TELEMETRY
#define REMOTE_ENABLE_POWER_TELEMETRY 1
#endif

#ifndef HA_POWER_EVENT_ENTITY_ID
#define HA_POWER_EVENT_ENTITY_ID "input_text.lily_remote_power_event"
#endif

#ifndef HA_POWER_LOG_ENTITY_ID
#define HA_POWER_LOG_ENTITY_ID "input_text.lily_remote_power_log"
#endif

#ifndef HA_POWER_VOLTAGE_ENTITY_ID
#define HA_POWER_VOLTAGE_ENTITY_ID "input_number.lily_remote_battery_voltage_mv"
#endif

#ifndef HA_POWER_SOC_ENTITY_ID
#define HA_POWER_SOC_ENTITY_ID "input_number.lily_remote_raw_soc"
#endif

#ifndef HA_POWER_WAKE_COUNT_ENTITY_ID
#define HA_POWER_WAKE_COUNT_ENTITY_ID "input_number.lily_remote_wake_count"
#endif

#ifndef REMOTE_POWER_AUDIT_MODE
#define REMOTE_POWER_AUDIT_MODE 0
#endif

#ifndef REMOTE_POWER_AUDIT_TIMER_MINUTES
#define REMOTE_POWER_AUDIT_TIMER_MINUTES 30
#endif

#ifndef REMOTE_POWER_AUDIT_PROFILE
#define REMOTE_POWER_AUDIT_PROFILE kPowerAuditProfileBaseline
#endif

#ifndef REMOTE_POWER_AUDIT_PROFILE_CYCLES
#define REMOTE_POWER_AUDIT_PROFILE_CYCLES 3
#endif

#ifndef REMOTE_POWER_AUDIT_ACTIVE_LOG_SECONDS
#define REMOTE_POWER_AUDIT_ACTIVE_LOG_SECONDS 60
#endif

#ifndef REMOTE_POWER_AUDIT_RESET_HOLD_SECONDS
#define REMOTE_POWER_AUDIT_RESET_HOLD_SECONDS (5UL * 60UL)
#endif

#ifndef REMOTE_POWER_AUDIT_UNPLUG_DEBOUNCE_SECONDS
#define REMOTE_POWER_AUDIT_UNPLUG_DEBOUNCE_SECONDS 5
#endif

struct PowerTelemetryRtcState {
  uint32_t magic = 0;
  uint32_t eventSeq = 0;
  uint32_t wakeCount = 0;
  uint16_t lastWakeVoltageMv = 0;
  uint16_t lastSleepVoltageMv = 0;
  int16_t lastRawSoc = -1;
  uint32_t lastAwakeMs = 0;
  uint32_t consecutiveExt0Wakes = 0;
  uint32_t consecutiveFastWakes = 0;
  uint32_t sleepAbortCount = 0;
  uint8_t lastWakeCause = 0;
  uint8_t lastIrqLevel = 0;
};

constexpr uint32_t kPowerTelemetryMagic = 0x4C525057; // "LRPW"
RTC_DATA_ATTR PowerTelemetryRtcState g_powerRtc;
String g_pendingWakeTelemetry;

HaClient g_haClient(HA_BASE_URL, HA_TOKEN);
PowerManager g_powerManager;
RemoteDisplayStatus g_displayStatus;
UiPageId g_currentPage = UiPageId::Home;
String g_dummyHelperState = "unknown";
String g_uiMessage = "Ready";
String g_homeMessage = "Tap to see what would run.";
String g_deviceControlMessage = "Tap to see what would run.";
RemoteDeviceTarget g_currentDeviceTarget = RemoteDeviceTarget::Telia;
bool g_lastActionOk = true;
bool g_sleepRequested = false;
uint32_t g_lastServiceCallMs = 0;
uint32_t g_lastActivityMs = 0;
uint32_t g_sleepHoldUntilMs = 0;

uint8_t activePowerAuditProfile() {
#if REMOTE_POWER_AUDIT_PROFILE == 99
  constexpr uint8_t kProfileCount = 6;
  constexpr uint32_t kCycles = REMOTE_POWER_AUDIT_PROFILE_CYCLES > 0 ? REMOTE_POWER_AUDIT_PROFILE_CYCLES : 1;
  const uint32_t wakeIndex = g_powerRtc.wakeCount > 0 ? g_powerRtc.wakeCount - 1 : 0;
  return static_cast<uint8_t>((wakeIndex / kCycles) % kProfileCount);
#else
  return static_cast<uint8_t>(REMOTE_POWER_AUDIT_PROFILE);
#endif
}

const char *powerAuditProfileName(uint8_t profile) {
  switch (profile) {
  case kPowerAuditProfileEspCleanup:
    return "esp";
  case kPowerAuditProfileTouchSleep:
    return "touch";
  case kPowerAuditProfileEpdPowerOff:
    return "epd";
  case kPowerAuditProfileI2cHighZ:
    return "i2c_hiz";
  case kPowerAuditProfileFullCleanup:
    return "full";
  case kPowerAuditProfileBaseline:
  default:
    return "base";
  }
}

bool isPlaceholder(const char *value) {
  if (value == nullptr || value[0] == '\0') {
    return true;
  }

  String text(value);
  return text.startsWith("your-") || text.startsWith("paste-");
}

bool validateConfig() {
  bool ok = true;

  if (isPlaceholder(WIFI_SSID) || isPlaceholder(WIFI_PASSWORD)) {
    logError("Config error: set WIFI_SSID and WIFI_PASSWORD in include/config.h");
    ok = false;
  }

  if (isPlaceholder(HA_BASE_URL) || isPlaceholder(HA_TOKEN)) {
    logError("Config error: set HA_BASE_URL and HA_TOKEN in include/config.h");
    ok = false;
  }

  return ok;
}

bool connectWifi(RemoteDisplayStatus &displayStatus) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to WiFi SSID '%s'", WIFI_SSID);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    logPrintf(LogLevel::Error, "WiFi failed, status=%d", WiFi.status());
    displayStatus.wifiConnected = false;
    return false;
  }

  displayStatus.wifiConnected = true;
  displayStatus.ipAddress = WiFi.localIP().toString();
  displayStatus.rssi = WiFi.RSSI();

  logPrintf(LogLevel::Info,
            "WiFi connected: %s RSSI=%d dBm",
            displayStatus.ipAddress.c_str(),
            displayStatus.rssi);
  return true;
}

void printHomeAssistantStatus(HaClient &haClient, RemoteDisplayStatus &displayStatus) {
  if (haClient.getApiMessage(displayStatus.haMessage)) {
    displayStatus.haApiOk = true;
    logPrintf(LogLevel::Info, "Home Assistant API: %s", displayStatus.haMessage.c_str());
  } else {
    displayStatus.haApiOk = false;
  }

  displayStatus.entityId = HA_TEST_ENTITY_ID;
  if (haClient.getEntityState(HA_TEST_ENTITY_ID, displayStatus.entityState)) {
    displayStatus.entityOk = true;
    logPrintf(LogLevel::Info,
              "%s = %s",
              displayStatus.entityId.c_str(),
              displayStatus.entityState.c_str());
  } else {
    displayStatus.entityOk = false;
  }
}

bool connectHomeAssistantApi(RemoteDisplayStatus &displayStatus) {
  if (!connectWifi(displayStatus)) {
    return false;
  }

  if (g_haClient.getApiMessage(displayStatus.haMessage)) {
    displayStatus.haApiOk = true;
    logPrintf(LogLevel::Info, "Home Assistant API: %s", displayStatus.haMessage.c_str());
    return true;
  }

  displayStatus.haApiOk = false;
  logError("Home Assistant API check failed");
  return false;
}

void runHomeAssistantWriteTest(HaClient &haClient) {
#if REMOTE_ENABLE_HA_WRITE_TEST
  logInfo("Running safe Home Assistant write test...");

  // Pick up helper entities that were deployed to package YAML.
  haClient.postJson("/api/services/input_boolean/reload");
  haClient.postJson("/api/services/input_text/reload");
  delay(1000);

  String before;
  if (!haClient.getEntityState(HA_WRITE_TEST_ENTITY_ID, before)) {
    logPrintf(LogLevel::Error, "Write test target missing: %s", HA_WRITE_TEST_ENTITY_ID);
    return;
  }

  logPrintf(LogLevel::Info, "%s before toggle = %s", HA_WRITE_TEST_ENTITY_ID, before.c_str());

  const String body = String("{\"entity_id\":\"") + HA_WRITE_TEST_ENTITY_ID + "\"}";
  if (!haClient.postJson("/api/services/input_boolean/toggle", body)) {
    return;
  }
  delay(500);

  String after;
  if (haClient.getEntityState(HA_WRITE_TEST_ENTITY_ID, after)) {
    logPrintf(LogLevel::Info, "%s after toggle = %s", HA_WRITE_TEST_ENTITY_ID, after.c_str());
  }
#else
  logInfo("Home Assistant write test disabled.");
#endif
}

bool refreshDummyHelperState() {
  if (!g_displayStatus.wifiConnected || !g_displayStatus.haApiOk) {
    g_dummyHelperState = "unavailable";
    return false;
  }

  if (!g_haClient.getEntityState(HA_WRITE_TEST_ENTITY_ID, g_dummyHelperState)) {
    g_dummyHelperState = "missing/error";
    logPrintf(LogLevel::Error, "%s state refresh failed", HA_WRITE_TEST_ENTITY_ID);
    return false;
  }
  logPrintf(LogLevel::Info, "%s = %s", HA_WRITE_TEST_ENTITY_ID, g_dummyHelperState.c_str());
  return true;
}

bool writeDummyTextLog(const String &value) {
  return g_haClient.callEntityServiceWithStringField("input_text",
                                                     "set_value",
                                                     HA_WRITE_TEST_TEXT_ENTITY_ID,
                                                     "value",
                                                     value);
}

String currentActivityFromSummary() {
  if (!g_displayStatus.entityOk || g_displayStatus.entityState.length() == 0) {
    return "Unknown";
  }

  const int separator = g_displayStatus.entityState.indexOf('|');
  String activity = separator >= 0 ? g_displayStatus.entityState.substring(0, separator) : g_displayStatus.entityState;
  activity.trim();
  return activity;
}

bool isAfterMillis(uint32_t value, uint32_t reference) {
  return static_cast<int32_t>(value - reference) > 0;
}

bool sleepHoldActive(uint32_t now = millis()) {
  return g_sleepHoldUntilMs != 0 && !isAfterMillis(now, g_sleepHoldUntilMs);
}

void holdSleepForRecovery(const char *reason, uint32_t holdMs = REMOTE_SLEEP_RECOVERY_HOLD_MS) {
  const uint32_t now = millis();
  g_sleepHoldUntilMs = now + holdMs;
  g_lastActivityMs = now;
  logPrintf(LogLevel::Warn,
            "Sleep recovery hold active for %u ms: %s",
            static_cast<unsigned>(holdMs),
            reason != nullptr ? reason : "unknown");
}

void markActivity(uint32_t timestampMs = millis()) {
  g_lastActivityMs = timestampMs;
}

void syncTouchActivity() {
  const uint32_t touchActivityMs = getRemoteTouchLastActivityMs();
  if (touchActivityMs != 0 && isAfterMillis(touchActivityMs, g_lastActivityMs)) {
    g_lastActivityMs = touchActivityMs;
  }
}

void initPowerTelemetryRtc() {
  if (g_powerRtc.magic == kPowerTelemetryMagic) {
    return;
  }

  g_powerRtc = PowerTelemetryRtcState{};
  g_powerRtc.magic = kPowerTelemetryMagic;
}

const char *wakeCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
  case ESP_SLEEP_WAKEUP_EXT0:
    return "ext0";
  case ESP_SLEEP_WAKEUP_EXT1:
    return "ext1";
  case ESP_SLEEP_WAKEUP_TIMER:
    return "timer";
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    return "touchpad";
  case ESP_SLEEP_WAKEUP_ULP:
    return "ulp";
  case ESP_SLEEP_WAKEUP_GPIO:
    return "gpio";
  case ESP_SLEEP_WAKEUP_UART:
    return "uart";
  case ESP_SLEEP_WAKEUP_UNDEFINED:
  default:
    return "cold";
  }
}

String trimTelemetryLine(String line) {
  constexpr size_t kMaxInputTextLength = 255;
  if (line.length() > kMaxInputTextLength) {
    line.remove(kMaxInputTextLength);
  }
  return line;
}

bool writeInputText(const char *entityId, const String &value) {
  return g_haClient.callEntityServiceWithStringField("input_text", "set_value", entityId, "value", trimTelemetryLine(value));
}

bool writeInputNumber(const char *entityId, int32_t value) {
  String body = String("{\"entity_id\":\"") + entityId + "\",\"value\":" + value + "}";
  return g_haClient.callService("input_number", "set_value", body);
}

bool sendPowerTelemetryToHa(const String &line, const RemoteBatteryReading &reading) {
#if REMOTE_ENABLE_POWER_TELEMETRY
  if (!g_displayStatus.wifiConnected || !g_displayStatus.haApiOk || line.length() == 0) {
    return false;
  }

  bool ok = writeInputText(HA_POWER_EVENT_ENTITY_ID, line);

  String oldLog;
  String newLog = line;
  if (g_haClient.getEntityState(HA_POWER_LOG_ENTITY_ID, oldLog) && oldLog.length() > 0 && oldLog != "unknown") {
    newLog += " | ";
    newLog += oldLog;
  }
  ok = writeInputText(HA_POWER_LOG_ENTITY_ID, newLog) && ok;

  if (reading.voltageKnown) {
    ok = writeInputNumber(HA_POWER_VOLTAGE_ENTITY_ID, reading.voltageMv) && ok;
  }
  if (reading.available) {
    ok = writeInputNumber(HA_POWER_SOC_ENTITY_ID, reading.percent) && ok;
  }
  ok = writeInputNumber(HA_POWER_WAKE_COUNT_ENTITY_ID, static_cast<int32_t>(g_powerRtc.wakeCount)) && ok;

  logPrintf(ok ? LogLevel::Info : LogLevel::Warn,
            "Power telemetry HA upload %s: %s",
            ok ? "ok" : "failed",
            line.c_str());
  return ok;
#else
  (void)line;
  (void)reading;
  return false;
#endif
}

int16_t estimateBatteryPercentFromVoltage(uint16_t voltageMv) {
  struct Point {
    uint16_t mv;
    int16_t percent;
  };

  // Conservative resting-voltage approximation for a single-cell Li-ion pack.
  // Used for the display fill only; the UI shows coarse bands to avoid fake precision.
  static constexpr Point kCurve[] = {
      {3300, 0},
      {3600, 10},
      {3700, 20},
      {3750, 30},
      {3800, 40},
      {3850, 50},
      {3920, 60},
      {3980, 70},
      {4050, 80},
      {4110, 90},
      {4200, 100},
  };

  if (voltageMv <= kCurve[0].mv) {
    return kCurve[0].percent;
  }
  for (size_t i = 1; i < sizeof(kCurve) / sizeof(kCurve[0]); ++i) {
    if (voltageMv <= kCurve[i].mv) {
      const Point low = kCurve[i - 1];
      const Point high = kCurve[i];
      const uint16_t spanMv = high.mv - low.mv;
      const int16_t spanPercent = high.percent - low.percent;
      return low.percent + static_cast<int32_t>(voltageMv - low.mv) * spanPercent / spanMv;
    }
  }
  return 100;
}

const char *batteryBandFromVoltage(uint16_t voltageMv) {
  if (voltageMv >= 3900) {
    return "HIGH";
  }
  if (voltageMv >= 3650) {
    return "MED";
  }
  return "LOW";
}

const char *batteryBandFromPercent(int16_t percent) {
  if (percent >= 60) {
    return "HIGH";
  }
  if (percent >= 25) {
    return "MED";
  }
  return "LOW";
}

void setBatteryDisplayEstimate(int16_t percent, const char *label, bool voltageBased) {
  const int16_t clamped = constrain(percent, 0, 100);
  g_displayStatus.batteryDisplayOverride = voltageBased;
  g_displayStatus.batteryDisplayPercent = clamped;
  g_displayStatus.batteryDisplayLabel = label;
}

void updateBatteryDisplayStatus(const RemoteBatteryReading &reading) {
  g_displayStatus.batteryKnown = reading.available;
  g_displayStatus.batteryPercent = reading.available ? reading.percent : -1;
  g_displayStatus.batteryDisplayOverride = false;
  g_displayStatus.batteryDisplayPercent = reading.available ? reading.percent : -1;
  g_displayStatus.batteryDisplayLabel = "";
  g_displayStatus.chargerKnown = reading.chargerAvailable;
  g_displayStatus.externalPower = reading.externalPower;
  g_displayStatus.batteryCharging = reading.chargeStatus == RemoteChargeStatus::PreCharge ||
                                    reading.chargeStatus == RemoteChargeStatus::FastCharging;
  g_displayStatus.batteryChargeDone = reading.chargeStatus == RemoteChargeStatus::ChargeDone;

  // BQ27220 SOC has stayed untrustworthy after repeated charge cycles. Best UX
  // here is honest coarse state from voltage, while raw SOC stays in telemetry.
  if (reading.voltageKnown) {
    const int16_t voltageEstimate = estimateBatteryPercentFromVoltage(reading.voltageMv);
    setBatteryDisplayEstimate(voltageEstimate, batteryBandFromVoltage(reading.voltageMv), true);
    return;
  }

  if (reading.available) {
    setBatteryDisplayEstimate(reading.percent, batteryBandFromPercent(reading.percent), false);
  }
}

String buildPowerTelemetryLine(const char *eventName,
                               const RemoteBatteryReading &reading,
                               uint32_t awakeMs,
                               bool displayOverride,
                               int16_t displayPercent) {
  const auto wakeCause = esp_sleep_get_wakeup_cause();
  const int irqLevel = gpio_get_level(static_cast<gpio_num_t>(REMOTE_TOUCH_WAKE_GPIO));

  String line = String("seq=") + g_powerRtc.eventSeq +
                " ev=" + eventName +
                " wake=" + g_powerRtc.wakeCount +
                " cause=" + wakeCauseName(wakeCause) +
                " rst=" + static_cast<int>(esp_reset_reason()) +
                " irq=" + irqLevel;

  if (reading.voltageKnown) {
    line += String(" mv=") + reading.voltageMv;
  }
  if (reading.available) {
    line += String(" soc=") + reading.percent;
  }
  if (displayPercent >= 0) {
    line += String(" disp=") + displayPercent;
  }
  if (g_displayStatus.batteryDisplayLabel.length() > 0) {
    line += String(" band=") + g_displayStatus.batteryDisplayLabel;
  }
  if (displayOverride) {
    line += " bsrc=mv";
  }
  if (reading.remainingCapacityKnown && reading.fullChargeCapacityKnown) {
    line += String(" rm=") + reading.remainingCapacityMah + "/" + reading.fullChargeCapacityMah;
  }
  if (reading.averageCurrentKnown) {
    line += String(" avg=") + reading.averageCurrentMa;
  }
  if (reading.chargerAvailable) {
    line += String(" chg=") + chargeStatusName(reading.chargeStatus);
    line += String(" pwr=") + (reading.externalPower ? 1 : 0);
  }
  if (reading.chargerFaultKnown) {
    line += String(" cf=0x") + String(reading.rawChargerFault, HEX);
  }
  if (awakeMs > 0) {
    line += String(" awake=") + awakeMs;
  }
#if REMOTE_POWER_AUDIT_MODE == 0
  if (strstr(eventName, "sleep") != nullptr) {
    line += " policy=touch";
  }
#endif
  if (g_powerRtc.lastSleepVoltageMv > 0) {
    line += String(" prev_sleep_mv=") + g_powerRtc.lastSleepVoltageMv;
  }
  if (reading.voltageKnown && g_powerRtc.lastSleepVoltageMv > 0) {
    line += String(" vdelta=") + (static_cast<int32_t>(reading.voltageMv) - g_powerRtc.lastSleepVoltageMv);
  }
#if REMOTE_POWER_AUDIT_MODE != 0
  const uint8_t profile = activePowerAuditProfile();
  line += String(" audit=") + REMOTE_POWER_AUDIT_MODE;
  line += String(" prof=") + powerAuditProfileName(profile);
  line += String(" amin=") + REMOTE_POWER_AUDIT_TIMER_MINUTES;
#endif

  return trimTelemetryLine(line);
}

String recordPowerTelemetryEvent(const char *eventName, uint32_t awakeMs = 0, bool uploadToHa = false) {
  configureRemoteChargerForRemoteUse();

  RemoteBatteryReading reading;
  readRemoteBattery(reading);
  updateBatteryDisplayStatus(reading);

  const auto wakeCause = esp_sleep_get_wakeup_cause();
  g_powerRtc.lastWakeCause = static_cast<uint8_t>(wakeCause);
  g_powerRtc.lastIrqLevel = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(REMOTE_TOUCH_WAKE_GPIO)));

  ++g_powerRtc.eventSeq;
  if (strcmp(eventName, "wake") == 0) {
    ++g_powerRtc.wakeCount;
    const bool ext0Wake = wakeCause == ESP_SLEEP_WAKEUP_EXT0;
    if (ext0Wake) {
      ++g_powerRtc.consecutiveExt0Wakes;
      if (g_powerRtc.lastAwakeMs > 0 && g_powerRtc.lastAwakeMs < REMOTE_EXT0_FAST_WAKE_MS) {
        ++g_powerRtc.consecutiveFastWakes;
      } else {
        g_powerRtc.consecutiveFastWakes = 0;
      }
    } else {
      g_powerRtc.consecutiveExt0Wakes = 0;
      g_powerRtc.consecutiveFastWakes = 0;
    }
    if (reading.voltageKnown) {
      g_powerRtc.lastWakeVoltageMv = reading.voltageMv;
    }
  } else if (strcmp(eventName, "sleep_abort") == 0) {
    ++g_powerRtc.sleepAbortCount;
  } else if (strstr(eventName, "sleep") != nullptr) {
    if (reading.voltageKnown) {
      g_powerRtc.lastSleepVoltageMv = reading.voltageMv;
    }
    g_powerRtc.lastAwakeMs = awakeMs;
  }
  if (reading.available) {
    g_powerRtc.lastRawSoc = reading.percent;
  }

  const String line = buildPowerTelemetryLine(eventName,
                                             reading,
                                             awakeMs,
                                             g_displayStatus.batteryDisplayOverride,
                                             g_displayStatus.batteryDisplayPercent);
  logPrintf(LogLevel::Info, "Power telemetry: %s", line.c_str());
  if (uploadToHa) {
    sendPowerTelemetryToHa(line, reading);
  }
  return line;
}

void flushPendingWakeTelemetry() {
#if REMOTE_ENABLE_POWER_TELEMETRY
  if (g_pendingWakeTelemetry.length() == 0 || !g_displayStatus.wifiConnected || !g_displayStatus.haApiOk) {
    return;
  }

  RemoteBatteryReading reading;
  readRemoteBattery(reading);
  sendPowerTelemetryToHa(g_pendingWakeTelemetry, reading);
  g_pendingWakeTelemetry = "";
#endif
}

void enterRemoteSleep(const char *reason) {
  if (!g_powerManager.prepareTouchSleep()) {
    logPrintf(LogLevel::Warn, "Sleep aborted before commit: %s", reason != nullptr ? reason : "unknown");
    recordPowerTelemetryEvent("sleep_abort", millis(), true);
    g_sleepRequested = false;
    holdSleepForRecovery("touch sleep preflight failed");
    return;
  }

  const String eventName = String(reason) + "_sleep";
  recordPowerTelemetryEvent(eventName.c_str(), millis(), true);
  if (!g_powerManager.finishTouchSleep()) {
    logPrintf(LogLevel::Warn, "Sleep aborted at commit: %s", reason != nullptr ? reason : "unknown");
    recordPowerTelemetryEvent("sleep_abort", millis(), true);
    g_sleepRequested = false;
    holdSleepForRecovery("touch sleep commit failed");
  }
}

void updateBatteryForRender() {
  const bool chargerConfigured = configureRemoteChargerForRemoteUse();

  RemoteBatteryReading reading;
  readRemoteBattery(reading);
  updateBatteryDisplayStatus(reading);

  logPrintf(LogLevel::Info,
            "Battery gauge: soc=%s%s voltage=%s current=%s avg=%s RM/FCC=%s SOH=%s batt=0x%s op=0x%s",
            reading.available ? String(reading.percent).c_str() : "--",
            reading.available ? "%" : "",
            reading.voltageKnown ? (String(reading.voltageMv) + "mV").c_str() : "--",
            reading.currentKnown ? (String(reading.currentMa) + "mA").c_str() : "--",
            reading.averageCurrentKnown ? (String(reading.averageCurrentMa) + "mA").c_str() : "--",
            reading.remainingCapacityKnown && reading.fullChargeCapacityKnown
                ? (String(reading.remainingCapacityMah) + "/" + reading.fullChargeCapacityMah + "mAh").c_str()
                : "--",
            reading.stateOfHealthKnown ? (String(reading.stateOfHealthPercent) + "%").c_str() : "--",
            reading.batteryStatusKnown ? String(reading.batteryStatus, HEX).c_str() : "--",
            reading.operationStatusKnown ? String(reading.operationStatus, HEX).c_str() : "--");

  logPrintf(LogLevel::Info,
            "Battery charger: cfg=%s status=%s power=%s vbus=%u raw=0x%02x fault=0x%s vreg=%s ichg_lim=%s iterm=%s",
            chargerConfigured ? "ok" : "failed",
            reading.chargerAvailable ? chargeStatusName(reading.chargeStatus) : "unavailable",
            reading.externalPower ? "good" : "none",
            static_cast<unsigned>(reading.vbusStatus),
            static_cast<unsigned>(reading.rawChargerStatus),
            reading.chargerFaultKnown ? String(reading.rawChargerFault, HEX).c_str() : "--",
            reading.chargeVoltageLimitKnown ? (String(reading.chargeVoltageLimitMv) + "mV").c_str() : "--",
            reading.chargeCurrentLimitKnown ? (String(reading.chargeCurrentLimitMa) + "mA").c_str() : "--",
            reading.terminationCurrentKnown ? (String(reading.terminationCurrentMa) + "mA").c_str() : "--");

  if (g_displayStatus.batteryDisplayOverride) {
    logPrintf(LogLevel::Info,
              "Battery display override: raw=%d%% display=%s",
              g_displayStatus.batteryPercent,
              g_displayStatus.batteryDisplayLabel.c_str());
  }
}

bool externalPowerPresentForSleepCheck() {
  configureRemoteChargerForRemoteUse();
  RemoteBatteryReading reading;
  readRemoteBattery(reading);
  updateBatteryDisplayStatus(reading);
  return reading.chargerAvailable && reading.externalPower;
}

void maybeEnterIdleSleep() {
#if !REMOTE_IDLE_SLEEP_ENABLED
  return;
#endif
  if (g_sleepRequested) {
    return;
  }

  if (sleepHoldActive()) {
    return;
  }

  syncTouchActivity();
  const uint32_t now = millis();
  if (now - g_lastActivityMs < kIdleSleepTimeoutMs) {
    return;
  }

  if (externalPowerPresentForSleepCheck()) {
    holdSleepForRecovery("external power present");
    return;
  }

  g_sleepRequested = true;
  logPrintf(LogLevel::Info,
            "Idle for %u ms; entering sleep",
            static_cast<unsigned>(now - g_lastActivityMs));
  enterRemoteSleep("idle");
}

void renderStatusUi() {
  updateBatteryForRender();
  renderStatusPage(g_displayStatus);
}

void renderSafeControlUi() {
  updateBatteryForRender();
  RemoteSafeControlPage page;
  page.status = g_displayStatus;
  page.helperEntityId = HA_WRITE_TEST_ENTITY_ID;
  page.helperState = g_dummyHelperState;
  page.message = g_uiMessage;
  page.lastActionOk = g_lastActionOk;
  renderSafeControlPage(page);
}

void renderHomeUi() {
  updateBatteryForRender();
  RemoteActivitiesPage page;
  page.status = g_displayStatus;
  page.currentActivity = currentActivityFromSummary();
  page.message = g_homeMessage;
  page.realActionsEnabled = false;
  renderHomePage(page);
}

void renderDeviceControlUi() {
  updateBatteryForRender();
  RemoteDeviceControlPage page;
  page.status = g_displayStatus;
  page.target = g_currentDeviceTarget;
  page.message = g_deviceControlMessage;
  renderDeviceControlPage(page);
}

void renderLightsUi() {
  updateBatteryForRender();
  RemoteLightsPage page;
  page.status = g_displayStatus;
  page.activeScene = "tv"; // Dummy for now
  page.message = g_uiMessage;
  renderLightsPage(page);
}

void renderRoomUi() {
  updateBatteryForRender();
  RemoteRoomPage page;
  page.status = g_displayStatus;
  page.activityState = "Watch TV";
  page.tvState = "On · Telia";
  page.wiimState = "TV · Volume 20";
  page.ls60State = "Coax · 71";
  page.lightsState = "Watch TV";
  page.message = g_uiMessage;
  renderRoomPage(page);
}

void renderMoreUi() {
  updateBatteryForRender();
  RemoteMorePage page;
  page.status = g_displayStatus;
  page.message = g_uiMessage;
  renderMorePage(page);
}

void renderCurrentPage() {
  switch (g_currentPage) {
  case UiPageId::SafeControl:
#if REMOTE_ENABLE_SAFE_CONTROL_PAGE
    renderSafeControlUi();
#else
    renderStatusUi();
#endif
    break;
  case UiPageId::Home:
    renderHomeUi();
    break;
  case UiPageId::Media:
    renderDeviceControlUi();
    break;
  case UiPageId::Lights:
    renderLightsUi();
    break;
  case UiPageId::Room:
    renderRoomUi();
    break;
  case UiPageId::More:
    renderMoreUi();
    break;
  default:
    renderStatusUi();
    break;
  }
}

UiPageId nextPage(UiPageId page) {
  switch (page) {
  case UiPageId::Home:
    return UiPageId::Media;
  case UiPageId::Media:
    return UiPageId::Lights;
  case UiPageId::Lights:
    return UiPageId::Room;
  case UiPageId::Room:
    return UiPageId::More;
  case UiPageId::More:
    return UiPageId::Home;
  default:
    return UiPageId::Home;
  }
}

UiPageId previousPage(UiPageId page) {
  switch (page) {
  case UiPageId::Home:
    return UiPageId::More;
  case UiPageId::Media:
    return UiPageId::Home;
  case UiPageId::Lights:
    return UiPageId::Media;
  case UiPageId::Room:
    return UiPageId::Lights;
  case UiPageId::More:
    return UiPageId::Room;
  default:
    return UiPageId::Home;
  }
}

void switchPage(UiPageId page) {
  if (g_currentPage == page) {
    return;
  }
  g_currentPage = page;
  logPrintf(LogLevel::Info, "Switching UI page to %s", uiPageName(page));
  renderCurrentPage();
}

void handlePageSwipe(const RemoteTouchEvent &event) {
#if !REMOTE_ENABLE_TOUCH_TEST
  if (event.gesture != RemoteTouchGesture::HorizontalSwipe) {
    return;
  }

  switchPage(event.dx < 0 ? nextPage(g_currentPage) : previousPage(g_currentPage));
#else
  (void)event;
#endif
}
const char *activityScriptForTap(int16_t x, int16_t y) {
  if (kActivityWatchTvButton.contains(x, y)) {
    return "activity_watch_tv";
  }
  if (kActivityWatchPlexButton.contains(x, y)) {
    return "activity_watch_plex";
  }
  if (kActivityPs5Button.contains(x, y)) {
    return "activity_play_ps5";
  }
  if (kActivityMusicButton.contains(x, y)) {
    return "activity_stream_music";
  }
  if (kActivityRecordsButton.contains(x, y)) {
    return "activity_listen_records";
  }
  if (kMediaOffButton.contains(x, y)) {
    return "activity_all_off";
  }
  if (kQuickVolDown.contains(x, y)) return "remote_volume_down";
  if (kQuickPrev.contains(x, y)) return "remote_previous";
  if (kQuickPlay.contains(x, y)) return "remote_play_pause";
  if (kQuickNext.contains(x, y)) return "remote_next";
  if (kQuickVolUp.contains(x, y)) return "remote_volume_up";

  return nullptr;
}

bool pageForBottomNavTap(int16_t x, int16_t y, UiPageId &page) {
  if (kNavHome.contains(x, y)) {
    page = UiPageId::Home;
    return true;
  }
  if (kNavMedia.contains(x, y)) {
    page = UiPageId::Media;
    return true;
  }
  if (kNavLights.contains(x, y)) {
    page = UiPageId::Lights;
    return true;
  }
  if (kNavRoom.contains(x, y)) {
    page = UiPageId::Room;
    return true;
  }
  if (kNavMore.contains(x, y)) {
    page = UiPageId::More;
    return true;
  }
  return false;
}

void handleBottomNavTouch(const RemoteTouchEvent &event) {
#if !REMOTE_ENABLE_TOUCH_TEST
  if (event.gesture != RemoteTouchGesture::Tap) {
    return;
  }

  UiPageId page;
  if (!pageForBottomNavTap(event.end.screenX, event.end.screenY, page)) {
    return;
  }
  switchPage(page);
#else
  (void)event;
#endif
}

const char *deviceTargetLogName(RemoteDeviceTarget target) {
  switch (target) {
  case RemoteDeviceTarget::Telia:
    return "telia";
  case RemoteDeviceTarget::Wiim:
    return "wiim";
  case RemoteDeviceTarget::Tv:
    return "tv";
  case RemoteDeviceTarget::Ls60:
    return "ls60";
  }
  return "unknown";
}

bool targetForDeviceTabTap(int16_t x, int16_t y, RemoteDeviceTarget &target) {
  if (kTabTelia.contains(x, y)) {
    target = RemoteDeviceTarget::Telia;
    return true;
  }
  if (kTabWiim.contains(x, y)) {
    target = RemoteDeviceTarget::Wiim;
    return true;
  }
  if (kTabTv.contains(x, y)) {
    target = RemoteDeviceTarget::Tv;
    return true;
  }
  if (kTabLs60.contains(x, y)) {
    target = RemoteDeviceTarget::Ls60;
    return true;
  }
  return false;
}

bool executeDeviceActionForTap(RemoteDeviceTarget target, int16_t x, int16_t y, String &outLog) {
  if (target == RemoteDeviceTarget::Telia) {
    if (kTeliaUp.contains(x, y)) { outLog = "Telia Up"; return g_haClient.callScript("remote_telia_nav", "{\"button\":\"up\"}"); }
    if (kTeliaLeft.contains(x, y)) { outLog = "Telia Left"; return g_haClient.callScript("remote_telia_nav", "{\"button\":\"left\"}"); }
    if (kTeliaOk.contains(x, y)) { outLog = "Telia OK"; return g_haClient.callScript("remote_telia_nav", "{\"button\":\"ok\"}"); }
    if (kTeliaRight.contains(x, y)) { outLog = "Telia Right"; return g_haClient.callScript("remote_telia_nav", "{\"button\":\"right\"}"); }
    if (kTeliaDown.contains(x, y)) { outLog = "Telia Down"; return g_haClient.callScript("remote_telia_nav", "{\"button\":\"down\"}"); }
    if (kTeliaBack.contains(x, y)) { outLog = "Telia Back"; return g_haClient.callScript("remote_telia_nav", "{\"button\":\"back\"}"); }
    if (kTeliaHome.contains(x, y)) { outLog = "Telia Home"; return g_haClient.callScript("remote_telia_nav", "{\"button\":\"home\"}"); }
    if (kTeliaRewind.contains(x, y)) { outLog = "Telia Rewind"; return g_haClient.callScript("remote_telia_command", "{\"command\":\"MEDIA_REWIND\"}"); }
    if (kTeliaPlayPause.contains(x, y)) { outLog = "Telia Play/Pause"; return g_haClient.callScript("remote_telia_command", "{\"command\":\"MEDIA_PLAY_PAUSE\"}"); }
    if (kTeliaFastForward.contains(x, y)) { outLog = "Telia FastForward"; return g_haClient.callScript("remote_telia_command", "{\"command\":\"MEDIA_FAST_FORWARD\"}"); }
    if (kTeliaPlex.contains(x, y)) { outLog = "Telia Plex"; return g_haClient.callScript("remote_telia_launch_plex"); }
    if (kTeliaYouTube.contains(x, y)) { outLog = "Telia YouTube"; return g_haClient.callScript("remote_telia_launch_youtube"); }
    if (kTeliaSpotify.contains(x, y)) { outLog = "Telia Spotify"; return g_haClient.callScript("remote_telia_launch_spotify"); }
  } else if (target == RemoteDeviceTarget::Wiim) {
    if (kWiimVolDown.contains(x, y)) { outLog = "WiiM VolDown"; return g_haClient.callScript("remote_volume_down"); }
    if (kWiimMute.contains(x, y)) { outLog = "WiiM Mute"; return g_haClient.callScript("remote_mute"); }
    if (kWiimVolUp.contains(x, y)) { outLog = "WiiM VolUp"; return g_haClient.callScript("remote_volume_up"); }
    if (kWiimHdmi.contains(x, y)) { outLog = "WiiM HDMI"; return g_haClient.callScript("remote_wiim_select_hdmi"); }
    if (kWiimPhono.contains(x, y)) { outLog = "WiiM Phono"; return g_haClient.callScript("remote_wiim_select_phono"); }
    if (kWiimAux.contains(x, y)) { outLog = "WiiM Aux"; return g_haClient.callScript("remote_wiim_select_aux"); }
    if (kWiimWifi.contains(x, y)) { outLog = "WiiM WiFi"; return g_haClient.callScript("remote_wiim_select_wifi"); }
    if (kWiimPrev.contains(x, y)) { outLog = "WiiM Prev"; return g_haClient.callScript("remote_previous"); }
    if (kWiimPlay.contains(x, y)) { outLog = "WiiM Play"; return g_haClient.callScript("remote_play_pause"); }
    if (kWiimNext.contains(x, y)) { outLog = "WiiM Next"; return g_haClient.callScript("remote_next"); }
  } else if (target == RemoteDeviceTarget::Tv) {
    if (kTvPowerOn.contains(x, y)) { outLog = "TV Power On"; return g_haClient.callScript("remote_tv_power", "{\"power_action\":\"on\"}"); }
    if (kTvPowerToggle.contains(x, y)) { outLog = "TV Power Toggle"; return g_haClient.callScript("remote_tv_power", "{\"power_action\":\"toggle\"}"); }
    if (kTvPowerOff.contains(x, y)) { outLog = "TV Power Off"; return g_haClient.callScript("remote_tv_power", "{\"power_action\":\"off\"}"); }
    if (kTvSourceTelia.contains(x, y)) { outLog = "TV Source Telia"; return g_haClient.callScript("remote_tv_select_source", "{\"source\":\"Sagemcom Set-Top Box\"}"); }
    if (kTvSourcePs5.contains(x, y)) { outLog = "TV Source PS5"; return g_haClient.callScript("remote_tv_select_source", "{\"source\":\"PS5 Game Console\"}"); }
    if (kTvSourceHdmi4.contains(x, y)) { outLog = "TV Source HDMI4"; return g_haClient.callScript("remote_tv_select_source", "{\"source\":\"HDMI 4\"}"); }
    if (kTvSourceLive.contains(x, y)) { outLog = "TV Source Live"; return g_haClient.callScript("remote_tv_select_source", "{\"source\":\"Live TV\"}"); }
  } else if (target == RemoteDeviceTarget::Ls60) {
    if (kLs60Restore.contains(x, y)) { outLog = "LS60 Restore"; return g_haClient.callScript("remote_ls60_restore_unity_gain"); }
    if (kLs60Coax.contains(x, y)) { outLog = "LS60 Coax"; return g_haClient.callScript("remote_ls60_select_coaxial"); }
    if (kLs60Vol71.contains(x, y)) { outLog = "LS60 Vol 71"; return g_haClient.callScript("remote_ls60_set_volume", "{\"volume\":71}"); }
    if (kLs60Analog.contains(x, y)) { outLog = "LS60 Analog"; return g_haClient.callScript("remote_ls60_select_analog"); }
    if (kLs60Optical.contains(x, y)) { outLog = "LS60 Optical"; return g_haClient.callScript("remote_ls60_select_optical"); }
    if (kLs60Tv.contains(x, y)) { outLog = "LS60 TV"; return g_haClient.callScript("remote_ls60_select_tv"); }
    if (kLs60Bluetooth.contains(x, y)) { outLog = "LS60 Bluetooth"; return g_haClient.callScript("remote_ls60_select_bluetooth"); }
  }

  outLog = "";
  return false;
}

void handleDeviceControlTouch(const RemoteTouchEvent &event) {
#if !REMOTE_ENABLE_TOUCH_TEST
  if (g_currentPage != UiPageId::Media || event.gesture != RemoteTouchGesture::Tap) {
    return;
  }

  RemoteDeviceTarget target;
  if (targetForDeviceTabTap(event.end.screenX, event.end.screenY, target)) {
    if (target != g_currentDeviceTarget) {
      g_currentDeviceTarget = target;
      logPrintf(LogLevel::Info, "Switching device target to %s", deviceTargetLogName(target));
      renderDeviceControlUi();
    }
    return;
  }

  UiPageId navPage;
  if (pageForBottomNavTap(event.end.screenX, event.end.screenY, navPage)) {
    return;
  }

  String outLog;
  bool ok = executeDeviceActionForTap(g_currentDeviceTarget, event.end.screenX, event.end.screenY, outLog);
  
  if (outLog.isEmpty()) {
    logPrintf(LogLevel::Info,
              "Device control tap outside action at screen=(%d,%d)",
              event.end.screenX,
              event.end.screenY);
    return;
  }

  g_deviceControlMessage = outLog + (ok ? " OK" : " Failed");
  logPrintf(LogLevel::Info, "Device action: %s", g_deviceControlMessage.c_str());
  renderCurrentPage(); // Refresh to show the message status
#else
  (void)event;
#endif
}

void handleHomeTouch(const RemoteTouchEvent &event) {
#if !REMOTE_ENABLE_TOUCH_TEST
  if (g_currentPage != UiPageId::Home || event.gesture != RemoteTouchGesture::Tap) {
    return;
  }

  UiPageId navPage;
  if (pageForBottomNavTap(event.end.screenX, event.end.screenY, navPage)) {
    return;
  }

  const char *script = activityScriptForTap(event.end.screenX, event.end.screenY);
  if (script != nullptr) {
    logPrintf(LogLevel::Info, "Executing script: %s", script);
    g_homeMessage = String("Calling ") + script + "...";
    renderCurrentPage(); // Show feedback immediately

    if (g_haClient.callScript(script)) {
      g_homeMessage = String("Called ") + script + " OK";
    } else {
      g_homeMessage = String("Failed to call ") + script;
    }
    renderCurrentPage();
    return;
  }

  // TODO: quick controls
  logPrintf(LogLevel::Info,
            "Home page tap outside action at screen=(%d,%d)",
            event.end.screenX,
            event.end.screenY);
#else
  (void)event;
#endif
}

bool executeLightsActionForTap(int16_t x, int16_t y, String &outLog) {
  if (kSceneNormal.contains(x, y)) { outLog = "Scene Normal"; return g_haClient.callScript("remote_living_room_lights", "{\"preset\":\"bright\"}"); }
  if (kSceneWatchTV.contains(x, y)) { outLog = "Scene Watch TV"; return g_haClient.callScript("remote_living_room_lights", "{\"preset\":\"tv\"}"); }
  if (kSceneRelax.contains(x, y)) { outLog = "Scene Relax"; return g_haClient.callScript("remote_living_room_lights", "{\"preset\":\"relax\"}"); }
  
  if (kZoneAllOn.contains(x, y)) { outLog = "All Lights On"; return g_haClient.callScript("remote_living_room_lights", "{\"preset\":\"bright\"}"); }
  if (kZoneAllOff.contains(x, y)) { outLog = "All Lights Off"; return g_haClient.callScript("remote_living_room_lights", "{\"preset\":\"off\"}"); }
  
  if (kZoneHallwayOn.contains(x, y)) { outLog = "Hallway On"; return g_haClient.callEntityService("light", "turn_on", "light.hallway"); }
  if (kZoneHallwayOff.contains(x, y)) { outLog = "Hallway Off"; return g_haClient.callEntityService("light", "turn_off", "light.hallway"); }
  if (kZoneKitchenOn.contains(x, y)) { outLog = "Kitchen On"; return g_haClient.callEntityService("light", "turn_on", "light.kitchen"); }
  if (kZoneKitchenOff.contains(x, y)) { outLog = "Kitchen Off"; return g_haClient.callEntityService("light", "turn_off", "light.kitchen"); }
  if (kZoneCornerOn.contains(x, y)) { outLog = "Corner On"; return g_haClient.callEntityService("light", "turn_on", "light.corner"); }
  if (kZoneCornerOff.contains(x, y)) { outLog = "Corner Off"; return g_haClient.callEntityService("light", "turn_off", "light.corner"); }
  if (kZoneDiningOn.contains(x, y)) { outLog = "Dining On"; return g_haClient.callEntityService("light", "turn_on", "light.dining"); }
  if (kZoneDiningOff.contains(x, y)) { outLog = "Dining Off"; return g_haClient.callEntityService("light", "turn_off", "light.dining"); }
  if (kZoneTvOn.contains(x, y)) { outLog = "TV Zone On"; return g_haClient.callEntityService("light", "turn_on", "light.tv_zone"); }
  if (kZoneTvOff.contains(x, y)) { outLog = "TV Zone Off"; return g_haClient.callEntityService("light", "turn_off", "light.tv_zone"); }
  
  outLog = "";
  return false;
}

bool executeRoomActionForTap(int16_t x, int16_t y, String &outLog) {
  if (kRoomFixLS60.contains(x, y)) { outLog = "Room Fix LS60"; return g_haClient.callScript("remote_ls60_restore_unity_gain"); }
  if (kRoomRefresh.contains(x, y)) { outLog = "Room Refresh"; return g_haClient.callScript("remote_status_refresh"); }
  outLog = "";
  return false;
}

bool executeMoreActionForTap(int16_t x, int16_t y, String &outLog) {
  if (kMoreAllOff.contains(x, y)) { outLog = "More All Off"; return g_haClient.callScript("activity_all_off"); }
  if (kMoreRefresh.contains(x, y)) { outLog = "More Refresh"; return g_haClient.callScript("remote_status_refresh"); }
  if (kMoreFixLS60.contains(x, y)) { outLog = "More Fix LS60"; return g_haClient.callScript("remote_ls60_restore_unity_gain"); }
  if (kMoreSleep.contains(x, y)) {
    outLog = "Sleep Remote"; 
    logPrintf(LogLevel::Info, "Executing deep sleep command from More page");
    enterRemoteSleep("manual");
    return true;
  }
  outLog = "";
  return false;
}

void handleLightsTouch(const RemoteTouchEvent &event) {
#if !REMOTE_ENABLE_TOUCH_TEST
  if (g_currentPage != UiPageId::Lights || event.gesture != RemoteTouchGesture::Tap) {
    return;
  }

  UiPageId navPage;
  if (pageForBottomNavTap(event.end.screenX, event.end.screenY, navPage)) {
    return;
  }

  String outLog;
  bool ok = executeLightsActionForTap(event.end.screenX, event.end.screenY, outLog);
  if (!outLog.isEmpty()) {
    g_uiMessage = outLog + (ok ? " OK" : " Failed");
    logPrintf(LogLevel::Info, "Lights action: %s", g_uiMessage.c_str());
    renderCurrentPage();
    return;
  }

  logPrintf(LogLevel::Info, "Lights page tap at (%d, %d)", event.end.screenX, event.end.screenY);
#else
  (void)event;
#endif
}

void handleRoomTouch(const RemoteTouchEvent &event) {
#if !REMOTE_ENABLE_TOUCH_TEST
  if (g_currentPage != UiPageId::Room || event.gesture != RemoteTouchGesture::Tap) {
    return;
  }

  UiPageId navPage;
  if (pageForBottomNavTap(event.end.screenX, event.end.screenY, navPage)) {
    return;
  }

  String outLog;
  bool ok = executeRoomActionForTap(event.end.screenX, event.end.screenY, outLog);
  if (!outLog.isEmpty()) {
    g_uiMessage = outLog + (ok ? " OK" : " Failed");
    logPrintf(LogLevel::Info, "Room action: %s", g_uiMessage.c_str());
    renderCurrentPage();
    return;
  }

  logPrintf(LogLevel::Info, "Room page tap at (%d, %d)", event.end.screenX, event.end.screenY);
#else
  (void)event;
#endif
}

void handleMoreTouch(const RemoteTouchEvent &event) {
#if !REMOTE_ENABLE_TOUCH_TEST
  if (g_currentPage != UiPageId::More || event.gesture != RemoteTouchGesture::Tap) {
    return;
  }

  UiPageId navPage;
  if (pageForBottomNavTap(event.end.screenX, event.end.screenY, navPage)) {
    return;
  }

  if (kMoreSafe.contains(event.end.screenX, event.end.screenY)) {
    switchPage(UiPageId::SafeControl);
    return;
  }

  String outLog;
  bool ok = executeMoreActionForTap(event.end.screenX, event.end.screenY, outLog);
  if (!outLog.isEmpty()) {
    g_uiMessage = outLog + (ok ? " OK" : " Failed");
    logPrintf(LogLevel::Info, "More action: %s", g_uiMessage.c_str());
    renderCurrentPage();
    return;
  }

  logPrintf(LogLevel::Info, "More page tap at (%d, %d)", event.end.screenX, event.end.screenY);
#else
  (void)event;
#endif
}

void handleSafeControlTouch(const RemoteTouchEvent &event) {
#if REMOTE_ENABLE_SAFE_CONTROL_PAGE && !REMOTE_ENABLE_TOUCH_TEST
  if (g_currentPage != UiPageId::SafeControl || event.gesture != RemoteTouchGesture::Tap) {
    return;
  }

  if (!kSafeControlToggleButton.contains(event.end.screenX, event.end.screenY)) {
    logPrintf(LogLevel::Info,
              "Safe control page tap outside button at screen=(%d,%d)",
              event.end.screenX,
              event.end.screenY);
    return;
  }

  const uint32_t now = millis();
  if (now - g_lastServiceCallMs < kServiceCallCooldownMs) {
    logInfo("Ignoring safe-control tap during cooldown");
    return;
  }
  g_lastServiceCallMs = now;

  logPrintf(LogLevel::Info, "Safe-control tap: toggling %s", HA_WRITE_TEST_ENTITY_ID);
  g_uiMessage = String("Toggling ") + HA_WRITE_TEST_ENTITY_ID + "...";
  g_lastActionOk = true;

  if (!g_haClient.callEntityService("input_boolean", "toggle", HA_WRITE_TEST_ENTITY_ID)) {
    g_uiMessage = String("Toggle failed for ") + HA_WRITE_TEST_ENTITY_ID;
    g_lastActionOk = false;
    refreshDummyHelperState();
    renderSafeControlUi();
    return;
  }

  delay(500);
  const bool stateOk = refreshDummyHelperState();
  const String textValue = String("dummy toggle -> ") + g_dummyHelperState + " @ " + String(millis()) + " ms";
  const bool textOk = writeDummyTextLog(textValue);

  if (stateOk && textOk) {
    g_uiMessage = String("Toggled OK. New state: ") + g_dummyHelperState;
    g_lastActionOk = true;
  } else if (stateOk) {
    g_uiMessage = String("Toggle OK, text log failed. State: ") + g_dummyHelperState;
    g_lastActionOk = false;
  } else {
    g_uiMessage = "Toggle sent, but state refresh failed";
    g_lastActionOk = false;
  }
  renderSafeControlUi();
#else
  (void)event;
#endif
}

bool refreshAuditPowerStatus() {
  RemoteBatteryReading reading;
  readRemoteBattery(reading);
  updateBatteryDisplayStatus(reading);
  return reading.chargerAvailable && reading.externalPower;
}

bool isAuditServiceWake() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED || esp_reset_reason() != ESP_RST_DEEPSLEEP;
}

void holdPowerAuditAwakeUntilUnplug(bool serviceWake) {
  const uint32_t logIntervalMs = REMOTE_POWER_AUDIT_ACTIVE_LOG_SECONDS * 1000UL;
  const uint32_t resetHoldMs = REMOTE_POWER_AUDIT_RESET_HOLD_SECONDS * 1000UL;
  const uint32_t unplugDebounceMs = REMOTE_POWER_AUDIT_UNPLUG_DEBOUNCE_SECONDS * 1000UL;
  const uint32_t startMs = millis();
  uint32_t lastLogMs = 0;
  uint32_t unpluggedSinceMs = 0;
  bool sawExternalPower = false;

  logPrintf(LogLevel::Info,
            "Power audit hold: external power keeps device awake; reset/cold boot hold is %u seconds",
            static_cast<unsigned>(REMOTE_POWER_AUDIT_RESET_HOLD_SECONDS));

  while (true) {
    const uint32_t now = millis();
    const bool externalPower = refreshAuditPowerStatus();
    if (externalPower) {
      sawExternalPower = true;
    }
    const bool resetHoldActive = serviceWake && !sawExternalPower && now - startMs < resetHoldMs;

    if (externalPower || resetHoldActive) {
      unpluggedSinceMs = 0;
    } else if (unpluggedSinceMs == 0) {
      unpluggedSinceMs = now;
    } else if (now - unpluggedSinceMs >= unplugDebounceMs) {
      logInfo("Power audit hold released; starting battery sleep test");
      break;
    }

    if (lastLogMs == 0 || now - lastLogMs >= logIntervalMs) {
      if (g_displayStatus.configOk &&
          (!g_displayStatus.wifiConnected || WiFi.status() != WL_CONNECTED || !g_displayStatus.haApiOk)) {
        connectHomeAssistantApi(g_displayStatus);
      }
      recordPowerTelemetryEvent(externalPower ? "audit_usb_hold" : "audit_reset_hold", now, g_displayStatus.configOk);
      lastLogMs = now;
    }

    delay(250);
  }
}

void runPowerAuditMode() {
  logPrintf(LogLevel::Info, "Power audit mode %d starting", REMOTE_POWER_AUDIT_MODE);

  if (!g_displayStatus.configOk) {
    logInfo("Power audit mode requires valid WiFi and HA config; staying awake for recovery");
    while (true) {
      delay(1000);
    }
  }

  const bool serviceWake = isAuditServiceWake();
  const bool externalPower = refreshAuditPowerStatus();
  connectHomeAssistantApi(g_displayStatus);
  flushPendingWakeTelemetry();

#if REMOTE_POWER_AUDIT_MODE != 3
  if (externalPower || serviceWake) {
    holdPowerAuditAwakeUntilUnplug(serviceWake);
  }
#endif

#if REMOTE_POWER_AUDIT_MODE == 3
  const uint32_t logIntervalMs = REMOTE_POWER_AUDIT_ACTIVE_LOG_SECONDS * 1000UL;
  uint32_t lastLogMs = millis();
  logInfo("Power audit active mode: staying awake and logging periodically");

  while (true) {
    if (millis() - lastLogMs >= logIntervalMs) {
      if ((!g_displayStatus.wifiConnected || WiFi.status() != WL_CONNECTED || !g_displayStatus.haApiOk)) {
        connectHomeAssistantApi(g_displayStatus);
      }
      recordPowerTelemetryEvent("audit_tick", millis(), true);
      lastLogMs = millis();
    }
    delay(100);
  }
#else
  const uint32_t timerSeconds = REMOTE_POWER_AUDIT_TIMER_MINUTES * 60UL;
  const uint8_t auditProfile = activePowerAuditProfile();
  recordPowerTelemetryEvent(REMOTE_POWER_AUDIT_MODE == 1 ? "timer_sleep" : "touch_sleep", millis(), true);
  g_powerManager.auditSleep(timerSeconds, REMOTE_POWER_AUDIT_MODE == 2, auditProfile);
#endif
}
} // namespace

void setupRemoteApp() {
  Serial.begin(kSerialBaud);
  delay(250);

  Serial.println();
  logPrintf(LogLevel::Info, "%s firmware %s", REMOTE_FIRMWARE_NAME, REMOTE_FIRMWARE_VERSION);
  logInfo("Board: LILYGO T5 E-Paper S3 Pro Lite / ESP32-S3");

  g_displayStatus.firmwareVersion = REMOTE_FIRMWARE_VERSION;
  g_displayStatus.entityId = HA_TEST_ENTITY_ID;
  g_displayStatus.writeTestEnabled = REMOTE_ENABLE_HA_WRITE_TEST;
  g_displayStatus.configOk = validateConfig();
  initPowerTelemetryRtc();
  g_pendingWakeTelemetry = recordPowerTelemetryEvent("wake");

  if (g_displayStatus.externalPower) {
    holdSleepForRecovery("external power present");
  } else if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
    holdSleepForRecovery("non-deep-sleep reset");
  } else if (g_powerRtc.consecutiveFastWakes >= REMOTE_EXT0_WAKE_LOOP_LIMIT) {
    holdSleepForRecovery("fast EXT0 wake loop");
  }

#if REMOTE_POWER_AUDIT_MODE != 0
  runPowerAuditMode();
  return;
#endif

  markActivity();
  initRemoteDisplay();

  if (!g_displayStatus.configOk) {
    logInfo("Create include/config.h from include/config.example.h, then rebuild.");
    renderStatusUi();
    markActivity();
    return;
  }

  if (connectWifi(g_displayStatus)) {
    printHomeAssistantStatus(g_haClient, g_displayStatus);
    flushPendingWakeTelemetry();
    runHomeAssistantWriteTest(g_haClient);
  }

#if REMOTE_ENABLE_TOUCH_TEST
  if (initRemoteTouch()) {
    renderTouchTestPage();
  } else {
    renderStatusUi();
  }
#else
#if REMOTE_ENABLE_SAFE_CONTROL_PAGE
  refreshDummyHelperState();
#endif
  renderCurrentPage();
  initRemoteTouch();
#endif

  markActivity();
  g_powerManager.maybeSleepAfterBoot(REMOTE_SLEEP_AFTER_BOOT);
}

void loopRemoteApp() {
  RemoteTouchEvent event;
  if (pollRemoteTouch(&event)) {
    markActivity();
    handlePageSwipe(event);
    handleBottomNavTouch(event);
    handleDeviceControlTouch(event);
    handleHomeTouch(event);
    handleLightsTouch(event);
    handleRoomTouch(event);
    handleMoreTouch(event);
    handleSafeControlTouch(event);
  }

  maybeEnterIdleSleep();
  delay(10);
}
