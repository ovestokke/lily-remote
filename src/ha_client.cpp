#include "ha_client.h"

#include <HTTPClient.h>

namespace {
constexpr uint32_t kHttpTimeoutMs = 10000;
}

HaClient::HaClient(const char *baseUrl, const char *token)
    : baseUrl_(baseUrl), token_(token) {}

String HaClient::makeUrl(const String &path) const {
  String base = baseUrl_;
  if (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }

  if (path.startsWith("/")) {
    return base + path;
  }
  return base + "/" + path;
}

bool HaClient::getJson(const String &path, JsonDocument &doc) {
  HTTPClient http;
  const String url = makeUrl(path);

  http.setTimeout(kHttpTimeoutMs);
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + token_);
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

bool HaClient::getApiMessage(String &message) {
  JsonDocument root;
  if (!getJson("/api/", root)) {
    return false;
  }
  message = root["message"] | "connected";
  return true;
}

bool HaClient::getEntityState(const char *entityId, String &state) {
  JsonDocument entity;
  const String entityPath = String("/api/states/") + entityId;
  if (!getJson(entityPath, entity)) {
    return false;
  }
  state = entity["state"] | "<no state>";
  return true;
}

bool HaClient::callService(const char *domain, const char *service, const String &body) {
  const String path = String("/api/services/") + domain + "/" + service;
  return postJson(path, body);
}

bool HaClient::callEntityService(const char *domain, const char *service, const char *entityId) {
  JsonDocument doc;
  doc["entity_id"] = entityId;

  String body;
  serializeJson(doc, body);
  return callService(domain, service, body);
}

bool HaClient::callEntityServiceWithStringField(const char *domain,
                                                const char *service,
                                                const char *entityId,
                                                const char *fieldName,
                                                const String &fieldValue) {
  JsonDocument doc;
  doc["entity_id"] = entityId;
  doc[fieldName] = fieldValue;

  String body;
  serializeJson(doc, body);
  return callService(domain, service, body);
}

bool HaClient::callScript(const char *scriptName, const String &variables) {
  String body = "{\"entity_id\":\"script.";
  body += scriptName;
  body += "\"";
  if (variables.length() > 0 && variables != "{}") {
    body += ",\"variables\":";
    body += variables;
  }
  body += "}";
  return postJson("/api/services/script/turn_on", body);
}

bool HaClient::postJson(const String &path, const String &body) {
  HTTPClient http;
  const String url = makeUrl(path);

  http.setTimeout(kHttpTimeoutMs);
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + token_);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  const int status = http.POST(body);
  if (status <= 0) {
    Serial.printf("HA POST %s failed: %s\n", path.c_str(), http.errorToString(status).c_str());
    http.end();
    return false;
  }

  const String response = http.getString();
  http.end();

  if (status < 200 || status >= 300) {
    Serial.printf("HA POST %s returned HTTP %d: %s\n", path.c_str(), status, response.c_str());
    return false;
  }

  Serial.printf("HA POST %s returned HTTP %d\n", path.c_str(), status);
  return true;
}
