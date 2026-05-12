#include "touch.h"

#include "i2c_bus.h"

#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvGT911.hpp>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

#ifndef REMOTE_ENABLE_I2C_SCAN
#define REMOTE_ENABLE_I2C_SCAN 0
#endif

namespace {
// T5 E-Paper S3 Pro / Pro Lite touch bus from vendor examples.
constexpr int kTouchIrq = 3;
constexpr int kTouchRst = 9;
constexpr uint8_t kGt911Address = GT911_SLAVE_ADDRESS_L;

// Display is in portrait with FastEPD rotation 90: logical screen 540x960.
constexpr int16_t kScreenWidth = 540;
constexpr int16_t kScreenHeight = 960;

TouchDrvGT911 g_touch;
bool g_touchReady = false;
bool g_wasPressed = false;
constexpr uint32_t kTouchReleaseDebounceMs = 80;
constexpr uint32_t kTouchMovePrintIntervalMs = 100;

uint32_t g_pressStartMs = 0;
uint32_t g_lastTouchMs = 0;
RemoteTouchPoint g_startPoint;
RemoteTouchPoint g_lastPoint;
uint32_t g_lastPrintMs = 0;

void scanTouchBus() {
  Serial.printf("Scanning I2C bus SDA=%d SCL=%d...\n", kRemoteI2cSda, kRemoteI2cScl);
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("  I2C device found at 0x%02X\n", address);
      ++found;
    }
  }
  Serial.printf("I2C scan complete: %u device(s) found\n", found);
}

RemoteTouchPoint mapTouchPoint(int16_t rawX, int16_t rawY) {
  RemoteTouchPoint point;
  point.rawX = rawX;
  point.rawY = rawY;

  // Calibration with the portrait touch-test page showed GT911 raw coordinates
  // already match FastEPD's rotation=90 logical portrait coordinates.
  point.screenX = constrain(rawX, 0, kScreenWidth - 1);
  point.screenY = constrain(rawY, 0, kScreenHeight - 1);
  return point;
}

void printPoint(const char *prefix, const RemoteTouchPoint &point) {
  Serial.printf("%s raw=(%d,%d) screen=(%d,%d)\n",
                prefix,
                point.rawX,
                point.rawY,
                point.screenX,
                point.screenY);
}

RemoteTouchEvent classifyGesture(const RemoteTouchPoint &start, const RemoteTouchPoint &end, uint32_t durationMs) {
  RemoteTouchEvent event;
  event.start = start;
  event.end = end;
  event.durationMs = durationMs;
  event.dx = end.screenX - start.screenX;
  event.dy = end.screenY - start.screenY;

  const int16_t absDx = abs(event.dx);
  const int16_t absDy = abs(event.dy);

  if (durationMs >= 700 && absDx < 40 && absDy < 40) {
    event.gesture = RemoteTouchGesture::LongPress;
  } else if (absDx >= 90 && absDx > absDy * 2) {
    event.gesture = RemoteTouchGesture::HorizontalSwipe;
  } else if (absDy >= 120 && absDy > absDx * 2) {
    event.gesture = RemoteTouchGesture::VerticalSwipe;
  } else {
    event.gesture = RemoteTouchGesture::Tap;
  }

  return event;
}

void printGesture(const RemoteTouchEvent &event) {
  switch (event.gesture) {
  case RemoteTouchGesture::LongPress:
    Serial.printf("Touch gesture: long_press duration=%u ms at screen=(%d,%d)\n",
                  static_cast<unsigned>(event.durationMs),
                  event.end.screenX,
                  event.end.screenY);
    break;
  case RemoteTouchGesture::HorizontalSwipe:
    Serial.printf("Touch gesture: horizontal_swipe %s dx=%d dy=%d duration=%u ms\n",
                  event.dx > 0 ? "right" : "left",
                  event.dx,
                  event.dy,
                  static_cast<unsigned>(event.durationMs));
    break;
  case RemoteTouchGesture::VerticalSwipe:
    Serial.printf("Touch gesture: vertical_swipe %s dx=%d dy=%d duration=%u ms\n",
                  event.dy > 0 ? "down" : "up",
                  event.dx,
                  event.dy,
                  static_cast<unsigned>(event.durationMs));
    break;
  case RemoteTouchGesture::Tap:
    Serial.printf("Touch gesture: tap duration=%u ms at screen=(%d,%d)\n",
                  static_cast<unsigned>(event.durationMs),
                  event.end.screenX,
                  event.end.screenY);
    break;
  case RemoteTouchGesture::None:
    break;
  }
}
} // namespace

bool initRemoteTouch() {
  if (g_touchReady) {
    return true;
  }

  Serial.println("Initializing GT911 touch...");
  initRemoteI2cBus();
#if REMOTE_ENABLE_I2C_SCAN
  scanTouchBus();
#endif

  g_touch.setPins(kTouchRst, kTouchIrq);
  if (!g_touch.begin(Wire, kGt911Address, kRemoteI2cSda, kRemoteI2cScl)) {
    Serial.printf("GT911 init failed at 0x%02X\n", kGt911Address);
    return false;
  }

  g_touch.setHomeButtonCallback([](void *) {
    Serial.println("Touch home button pressed");
  }, nullptr);
  g_touch.setInterruptMode(LOW_LEVEL_QUERY);

  g_touchReady = true;
  Serial.printf("GT911 touch initialized at 0x%02X\n", kGt911Address);
#if REMOTE_ENABLE_TOUCH_TEST
  Serial.println("Touch test: tap corners and swipe; serial will print raw/screen coordinates.");
#else
  Serial.println("Touch input ready.");
#endif
  return true;
}

bool isRemoteTouchReady() {
  return g_touchReady;
}

uint32_t getRemoteTouchLastActivityMs() {
  return g_lastTouchMs;
}

bool pollRemoteTouch(RemoteTouchEvent *event) {
  if (event != nullptr) {
    *event = RemoteTouchEvent{};
  }

  if (!g_touchReady) {
    return false;
  }

  int16_t xs[5] = {0};
  int16_t ys[5] = {0};
  const uint32_t now = millis();
  const uint8_t count = g_touch.getPoint(xs, ys, g_touch.getSupportTouchPoint());

  if (count > 0) {
    const RemoteTouchPoint point = mapTouchPoint(xs[0], ys[0]);
    g_lastPoint = point;
    g_lastTouchMs = now;

    if (!g_wasPressed) {
      g_wasPressed = true;
      g_pressStartMs = now;
      g_startPoint = point;
      g_lastPrintMs = now;
      printPoint("Touch down", point);
    } else if (now - g_lastPrintMs >= kTouchMovePrintIntervalMs) {
      printPoint("Touch move", point);
      g_lastPrintMs = now;
    }

    if (count > 1) {
      Serial.printf("Touch points: %u", count);
      for (uint8_t i = 0; i < count; ++i) {
        Serial.printf(" [%u]=(%d,%d)", i, xs[i], ys[i]);
      }
      Serial.println();
    }
    return false;
  }

  if (g_wasPressed && now - g_lastTouchMs >= kTouchReleaseDebounceMs) {
    const uint32_t durationMs = now - g_pressStartMs;
    RemoteTouchEvent classified = classifyGesture(g_startPoint, g_lastPoint, durationMs);
    printPoint("Touch up", g_lastPoint);
    printGesture(classified);
    g_wasPressed = false;
    if (event != nullptr) {
      *event = classified;
    }
    return true;
  }

  return false;
}
