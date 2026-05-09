#pragma once

#include <Arduino.h>

#include "ui.h"

struct RemoteDisplayStatus {
  const char *firmwareVersion = "unknown";
  bool configOk = true;
  bool wifiConnected = false;
  String ipAddress;
  int32_t rssi = 0;

  bool haApiOk = false;
  String haMessage;

  bool entityOk = false;
  String entityId;
  String entityState;

  bool writeTestEnabled = false;
};

struct RemoteSafeControlPage {
  RemoteDisplayStatus status;
  String helperEntityId;
  String helperState;
  String message;
  bool lastActionOk = true;
};

struct RemoteShellPage {
  UiPageId pageId = UiPageId::Info;
  const char *title = "Info";
  const char *subtitle = "";
  String primary;
  String secondary;
  String footerHint;
};

struct RemoteActivitiesPage {
  RemoteDisplayStatus status;
  String currentActivity;
  String message;
  bool realActionsEnabled = false;
};

enum class RemoteDeviceTarget : uint8_t {
  Telia,
  Wiim,
  Tv,
  Ls60,
};

struct RemoteDeviceControlPage {
  RemoteDisplayStatus status;
  RemoteDeviceTarget target = RemoteDeviceTarget::Telia;
  String message;
};

bool initRemoteDisplay();
void renderStatusPage(const RemoteDisplayStatus &status);
void renderSafeControlPage(const RemoteSafeControlPage &page);
void renderActivitiesPage(const RemoteActivitiesPage &page);
void renderDeviceControlPage(const RemoteDeviceControlPage &page);
void renderShellPage(const RemoteShellPage &page, const RemoteDisplayStatus &status);
void renderTouchTestPage();
