#include "app.h"

#include <Arduino.h>
#include <WiFi.h>

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

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint32_t kServiceCallCooldownMs = 1200;

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

HaClient g_haClient(HA_BASE_URL, HA_TOKEN);
PowerManager g_powerManager;
RemoteDisplayStatus g_displayStatus;
UiPageId g_currentPage = UiPageId::Activities;
String g_dummyHelperState = "unknown";
String g_uiMessage = "Ready";
String g_activityMessage = "Tap to see what would run.";
String g_deviceControlMessage = "Tap to see what would run.";
RemoteDeviceTarget g_currentDeviceTarget = RemoteDeviceTarget::Telia;
bool g_lastActionOk = true;
uint32_t g_lastServiceCallMs = 0;

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

void renderSafeControlUi() {
  RemoteSafeControlPage page;
  page.status = g_displayStatus;
  page.helperEntityId = HA_WRITE_TEST_ENTITY_ID;
  page.helperState = g_dummyHelperState;
  page.message = g_uiMessage;
  page.lastActionOk = g_lastActionOk;
  renderSafeControlPage(page);
}

void renderActivitiesUi() {
  RemoteActivitiesPage page;
  page.status = g_displayStatus;
  page.currentActivity = currentActivityFromSummary();
  page.message = g_activityMessage;
  page.realActionsEnabled = false;
  renderActivitiesPage(page);
}

void renderDeviceControlUi() {
  RemoteDeviceControlPage page;
  page.status = g_displayStatus;
  page.target = g_currentDeviceTarget;
  page.message = g_deviceControlMessage;
  renderDeviceControlPage(page);
}

RemoteShellPage makeShellPage(UiPageId pageId) {
  RemoteShellPage page;
  page.pageId = pageId;
  page.footerHint = "Swipe left/right. Real actions still disabled.";

  switch (pageId) {
  case UiPageId::Activities:
    page.title = "Activities";
    page.subtitle = "HA script contract confirmed";
    page.primary = "Watch TV: script.activity_watch_tv | PS5: script.activity_play_ps5 | Music: script.activity_stream_music";
    page.secondary = "Records: script.activity_listen_records | Off: script.activity_all_off. Buttons still disabled until UI action policy is added.";
    break;
  case UiPageId::Media:
    page.title = "Media";
    page.subtitle = "HA media helpers confirmed";
    page.primary = "Volume/mute/playback use script.remote_volume_up/down/mute/play_pause/next/previous.";
    page.secondary = "Source buttons use script.remote_select_hdmi and script.remote_select_phono. Volume should not full-refresh per tap.";
    break;
  case UiPageId::Lights:
    page.title = "Lights";
    page.subtitle = "HA lights helper confirmed";
    page.primary = "Use script.remote_living_room_lights with preset: bright, dimmed, relax, nightlight, read, tv, records, off.";
    page.secondary = "Next: choose which presets deserve large e-ink buttons before enabling scene calls.";
    break;
  case UiPageId::Info:
    page.title = "Info";
    page.subtitle = "Remote diagnostics";
    page.primary = String("WiFi: ") + (g_displayStatus.wifiConnected ? g_displayStatus.ipAddress : "offline") +
                   " RSSI " + String(g_displayStatus.rssi) + " dBm";
    page.secondary = String("FW: ") + g_displayStatus.firmwareVersion +
                     " | HA: " + (g_displayStatus.haApiOk ? g_displayStatus.haMessage : "unavailable");
    break;
  case UiPageId::Status:
  default:
    page.title = "Status";
    page.subtitle = "Home Assistant summary";
    page.primary = "Read-only status view for the living-room remote.";
    page.secondary = String("Test entity: ") + g_displayStatus.entityId;
    break;
  }
  return page;
}

void renderCurrentPage() {
  switch (g_currentPage) {
  case UiPageId::SafeControl:
#if REMOTE_ENABLE_SAFE_CONTROL_PAGE
    renderSafeControlUi();
#else
    renderShellPage(makeShellPage(UiPageId::Status), g_displayStatus);
#endif
    break;
  case UiPageId::Activities:
    renderActivitiesUi();
    break;
  case UiPageId::Media:
    renderDeviceControlUi();
    break;
  case UiPageId::Lights:
  case UiPageId::Info:
    renderShellPage(makeShellPage(g_currentPage), g_displayStatus);
    break;
  case UiPageId::Status:
  default:
    renderStatusPage(g_displayStatus);
    break;
  }
}

UiPageId nextPage(UiPageId page) {
  switch (page) {
  case UiPageId::SafeControl:
    return UiPageId::Activities;
  case UiPageId::Activities:
    return UiPageId::Media;
  case UiPageId::Media:
    return UiPageId::Lights;
  case UiPageId::Lights:
    return UiPageId::Info;
  case UiPageId::Info:
    return UiPageId::Status;
  case UiPageId::Status:
  default:
#if REMOTE_ENABLE_SAFE_CONTROL_PAGE
    return UiPageId::SafeControl;
#else
    return UiPageId::Activities;
#endif
  }
}

UiPageId previousPage(UiPageId page) {
  switch (page) {
  case UiPageId::SafeControl:
    return UiPageId::Status;
  case UiPageId::Activities:
#if REMOTE_ENABLE_SAFE_CONTROL_PAGE
    return UiPageId::SafeControl;
#else
    return UiPageId::Status;
#endif
  case UiPageId::Media:
    return UiPageId::Activities;
  case UiPageId::Lights:
    return UiPageId::Media;
  case UiPageId::Info:
    return UiPageId::Lights;
  case UiPageId::Status:
  default:
    return UiPageId::Info;
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
    return "script.activity_watch_tv";
  }
  if (kActivityPs5Button.contains(x, y)) {
    return "script.activity_play_ps5";
  }
  if (kActivityMusicButton.contains(x, y)) {
    return "script.activity_stream_music";
  }
  if (kActivityRecordsButton.contains(x, y)) {
    return "script.activity_listen_records";
  }
  if (kMediaOffButton.contains(x, y)) {
    return "script.activity_all_off";
  }
  return nullptr;
}

bool pageForBottomNavTap(int16_t x, int16_t y, UiPageId &page) {
  if (kBottomNavHomeButton.contains(x, y)) {
    page = UiPageId::Activities;
    return true;
  }
  if (kBottomNavMediaButton.contains(x, y)) {
    page = UiPageId::Media;
    return true;
  }
  if (kBottomNavLightsButton.contains(x, y)) {
    page = UiPageId::Lights;
    return true;
  }
  if (kBottomNavInfoButton.contains(x, y)) {
    page = UiPageId::Info;
    return true;
  }
  if (kBottomNavMoreButton.contains(x, y)) {
    page = UiPageId::Status;
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
  if (kDeviceTeliaTab.contains(x, y)) {
    target = RemoteDeviceTarget::Telia;
    return true;
  }
  if (kDeviceWiimTab.contains(x, y)) {
    target = RemoteDeviceTarget::Wiim;
    return true;
  }
  if (kDeviceTvTab.contains(x, y)) {
    target = RemoteDeviceTarget::Tv;
    return true;
  }
  if (kDeviceLs60Tab.contains(x, y)) {
    target = RemoteDeviceTarget::Ls60;
    return true;
  }
  return false;
}

const char *deviceActionForTap(RemoteDeviceTarget target, int16_t x, int16_t y) {
  if (target == RemoteDeviceTarget::Telia) {
    if (kNavBackButton.contains(x, y)) return "script.remote_telia_nav back";
    if (kNavHomeButton.contains(x, y)) return "script.remote_telia_nav home";
    if (kNavUpButton.contains(x, y)) return "script.remote_telia_nav up";
    if (kNavDownButton.contains(x, y)) return "script.remote_telia_nav down";
    if (kNavLeftButton.contains(x, y)) return "script.remote_telia_nav left";
    if (kNavRightButton.contains(x, y)) return "script.remote_telia_nav right";
    if (kNavOkButton.contains(x, y)) return "script.remote_telia_nav ok";
    return nullptr;
  }

  if (kDeviceWideButton.contains(x, y)) {
    switch (target) {
    case RemoteDeviceTarget::Wiim: return "script.remote_volume_up";
    case RemoteDeviceTarget::Tv: return "script.remote_tv_power toggle";
    case RemoteDeviceTarget::Ls60: return "script.remote_ls60_restore_unity_gain";
    default: return nullptr;
    }
  }
  if (kDeviceLeftTopButton.contains(x, y)) {
    switch (target) {
    case RemoteDeviceTarget::Wiim: return "script.remote_wiim_select_source TV";
    case RemoteDeviceTarget::Tv: return "script.remote_tv_select_source Sagemcom Set-Top Box";
    case RemoteDeviceTarget::Ls60: return "script.remote_ls60_select_source coaxial";
    default: return nullptr;
    }
  }
  if (kDeviceRightTopButton.contains(x, y)) {
    switch (target) {
    case RemoteDeviceTarget::Wiim: return "script.remote_wiim_select_source Phono In";
    case RemoteDeviceTarget::Tv: return "script.remote_tv_select_source PS5 Game Console";
    case RemoteDeviceTarget::Ls60: return "script.remote_ls60_set_volume 71";
    default: return nullptr;
    }
  }
  if (kDeviceLeftBottomButton.contains(x, y)) {
    switch (target) {
    case RemoteDeviceTarget::Wiim: return "script.remote_mute";
    case RemoteDeviceTarget::Tv: return "script.remote_tv_select_source Plex";
    case RemoteDeviceTarget::Ls60: return "ls60 volume_up recovery TODO";
    default: return nullptr;
    }
  }
  if (kDeviceRightBottomButton.contains(x, y)) {
    switch (target) {
    case RemoteDeviceTarget::Wiim: return "script.remote_volume_down";
    case RemoteDeviceTarget::Tv: return "script.remote_tv_select_source Netflix";
    case RemoteDeviceTarget::Ls60: return "ls60 volume_down recovery TODO";
    default: return nullptr;
    }
  }
  return nullptr;
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

  const char *action = deviceActionForTap(g_currentDeviceTarget, event.end.screenX, event.end.screenY);
  if (action == nullptr) {
    logPrintf(LogLevel::Info,
              "Device control tap outside action at screen=(%d,%d)",
              event.end.screenX,
              event.end.screenY);
    return;
  }

  g_deviceControlMessage = String("Would call ") + deviceTargetLogName(g_currentDeviceTarget) + "." + action + " (disabled)";
  logPrintf(LogLevel::Info,
            "Device action disabled: target=%s action=%s",
            deviceTargetLogName(g_currentDeviceTarget),
            action);
#else
  (void)event;
#endif
}

void handleActivitiesTouch(const RemoteTouchEvent &event) {
#if !REMOTE_ENABLE_TOUCH_TEST
  if (g_currentPage != UiPageId::Activities || event.gesture != RemoteTouchGesture::Tap) {
    return;
  }

  UiPageId navPage;
  if (pageForBottomNavTap(event.end.screenX, event.end.screenY, navPage)) {
    return;
  }

  const char *script = activityScriptForTap(event.end.screenX, event.end.screenY);
  if (script == nullptr) {
    logPrintf(LogLevel::Info,
              "Activities page tap outside action at screen=(%d,%d)",
              event.end.screenX,
              event.end.screenY);
    return;
  }

  g_activityMessage = String("Would call ") + script + " (disabled)";
  logPrintf(LogLevel::Info, "Activity action disabled: would call %s", script);
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

  initRemoteDisplay();

  if (!g_displayStatus.configOk) {
    logInfo("Create include/config.h from include/config.example.h, then rebuild.");
    renderStatusPage(g_displayStatus);
    return;
  }

  if (connectWifi(g_displayStatus)) {
    printHomeAssistantStatus(g_haClient, g_displayStatus);
    runHomeAssistantWriteTest(g_haClient);
  }

#if REMOTE_ENABLE_TOUCH_TEST
  if (initRemoteTouch()) {
    renderTouchTestPage();
  } else {
    renderStatusPage(g_displayStatus);
  }
#else
#if REMOTE_ENABLE_SAFE_CONTROL_PAGE
  refreshDummyHelperState();
#endif
  renderCurrentPage();
  initRemoteTouch();
#endif

  g_powerManager.maybeSleepAfterBoot(REMOTE_SLEEP_AFTER_BOOT);
}

void loopRemoteApp() {
  RemoteTouchEvent event;
  if (pollRemoteTouch(&event)) {
    handlePageSwipe(event);
    handleBottomNavTouch(event);
    handleDeviceControlTouch(event);
    handleActivitiesTouch(event);
    handleSafeControlTouch(event);
  }
  delay(10);
}
