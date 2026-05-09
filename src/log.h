#pragma once

#include <Arduino.h>

enum class LogLevel : uint8_t {
  Info,
  Warn,
  Error,
};

void logPrintf(LogLevel level, const char *format, ...);
void logInfo(const String &message);
void logWarn(const String &message);
void logError(const String &message);
