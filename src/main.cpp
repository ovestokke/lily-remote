#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint32_t kHttpTimeoutMs = 10000;

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
    Serial.println("Config error: set WIFI_SSID and WIFI_PASSWORD in include/config.h");
    ok = false;
  }

  if (isPlaceholder(HA_BASE_URL) || isPlaceholder(HA_TOKEN)) {
    Serial.println("Config error: set HA_BASE_URL and HA_TOKEN in include/config.h");
    ok = false;
  }

  return ok;
}

bool connectWifi() {
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
    Serial.printf("WiFi failed, status=%d\n", WiFi.status());
    return false;
  }

  Serial.printf("WiFi connected: %s RSSI=%d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

String makeHaUrl(const String &path) {
  String base = HA_BASE_URL;
  if (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }

  if (path.startsWith("/")) {
    return base + path;
  }
  return base + "/" + path;
}

bool haGetJson(const String &path, JsonDocument &doc) {
  HTTPClient http;
  const String url = makeHaUrl(path);

  http.setTimeout(kHttpTimeoutMs);
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Accept", "application/json");

  const int status = http.GET();
  if (status <= 0) {
    Serial.printf("HA GET %s failed: %s\n", path.c_str(), http.errorToString(status).c_str());
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();

  if (status < 200 || status >= 300) {
    Serial.printf("HA GET %s returned HTTP %d: %s\n", path.c_str(), status, body.c_str());
    return false;
  }

  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.printf("HA JSON parse failed: %s\n", error.c_str());
    return false;
  }

  return true;
}

void printHomeAssistantStatus() {
  JsonDocument root;
  if (haGetJson("/api/", root)) {
    Serial.printf("Home Assistant API: %s\n", root["message"] | "connected");
  }

  JsonDocument entity;
  const String entityPath = String("/api/states/") + HA_TEST_ENTITY_ID;
  if (haGetJson(entityPath, entity)) {
    Serial.printf("%s = %s\n",
                  HA_TEST_ENTITY_ID,
                  entity["state"] | "<no state>");
  }
}

void maybeSleep() {
#if REMOTE_SLEEP_AFTER_BOOT
  Serial.println("Sleeping for 60 seconds...");
  Serial.flush();
  esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
  esp_deep_sleep_start();
#endif
}
} // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(250);

  Serial.println();
  Serial.println("E-Ink Smart Home Remote bring-up firmware");
  Serial.println("Board: LILYGO T5 E-Paper S3 Pro Lite / ESP32-S3");

  if (!validateConfig()) {
    Serial.println("Create include/config.h from include/config.example.h, then rebuild.");
    return;
  }

  if (connectWifi()) {
    printHomeAssistantStatus();
  }

  maybeSleep();
}

void loop() {
  delay(1000);
}
