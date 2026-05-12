#include "app.h"

#include <Arduino.h>
#include <WiFi.h>

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

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint32_t kServiceCallCooldownMs = 1200;
constexpr uint32_t kIdleSleepTimeoutMs = 5UL * 60UL * 1000UL;

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

bool isAfterMillis(uint32_t value, uint32_t reference) {
  return static_cast<int32_t>(value - reference) > 0;
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

void updateBatteryForRender() {
  RemoteBatteryReading reading;
  if (readRemoteBattery(reading)) {
    g_displayStatus.batteryKnown = reading.available;
    g_displayStatus.batteryPercent = reading.percent;
    logPrintf(LogLevel::Info, "Battery: %d%%", g_displayStatus.batteryPercent);
  } else {
    g_displayStatus.batteryKnown = false;
    g_displayStatus.batteryPercent = -1;
    logInfo("Battery: unavailable");
  }
}

void maybeEnterIdleSleep() {
  if (g_sleepRequested) {
    return;
  }

  syncTouchActivity();
  const uint32_t now = millis();
  if (now - g_lastActivityMs < kIdleSleepTimeoutMs) {
    return;
  }

  g_sleepRequested = true;
  logPrintf(LogLevel::Info,
            "Idle for %u ms; entering sleep",
            static_cast<unsigned>(now - g_lastActivityMs));
  g_powerManager.goToSleep();
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
    g_powerManager.goToSleep();
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
