#pragma once

#include <Arduino.h>

struct RemoteTouchPoint {
  int16_t rawX = 0;
  int16_t rawY = 0;
  int16_t screenX = 0;
  int16_t screenY = 0;
};

enum class RemoteTouchGesture : uint8_t {
  None,
  Tap,
  LongPress,
  HorizontalSwipe,
  VerticalSwipe,
};

struct RemoteTouchEvent {
  RemoteTouchGesture gesture = RemoteTouchGesture::None;
  RemoteTouchPoint start;
  RemoteTouchPoint end;
  int16_t dx = 0;
  int16_t dy = 0;
  uint32_t durationMs = 0;
};

bool initRemoteTouch();
bool isRemoteTouchReady();
bool pollRemoteTouch(RemoteTouchEvent *event = nullptr);
