#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class HaClient {
public:
  HaClient(const char *baseUrl, const char *token);

  bool getApiMessage(String &message);
  bool getEntityState(const char *entityId, String &state);
  bool postJson(const String &path, const String &body = "{}");
  bool callService(const char *domain, const char *service, const String &body = "{}");
  bool callEntityService(const char *domain, const char *service, const char *entityId);
  bool callEntityServiceWithStringField(const char *domain,
                                        const char *service,
                                        const char *entityId,
                                        const char *fieldName,
                                        const String &fieldValue);

private:
  String makeUrl(const String &path) const;
  bool getJson(const String &path, JsonDocument &doc);

  const char *baseUrl_;
  const char *token_;
};
