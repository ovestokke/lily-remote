#include "log.h"

#include <stdarg.h>
#include <stdio.h>

namespace {
const char *labelFor(LogLevel level) {
  switch (level) {
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warn:
    return "WARN";
  case LogLevel::Error:
    return "ERROR";
  }
  return "INFO";
}
} // namespace

void logPrintf(LogLevel level, const char *format, ...) {
  Serial.printf("[%10lu] %-5s ", static_cast<unsigned long>(millis()), labelFor(level));

  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  Serial.print(buffer);

  const size_t len = strlen(buffer);
  if (len == 0 || buffer[len - 1] != '\n') {
    Serial.println();
  }
}

void logInfo(const String &message) {
  logPrintf(LogLevel::Info, "%s", message.c_str());
}

void logWarn(const String &message) {
  logPrintf(LogLevel::Warn, "%s", message.c_str());
}

void logError(const String &message) {
  logPrintf(LogLevel::Error, "%s", message.c_str());
}
