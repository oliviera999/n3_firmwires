#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>

namespace AutomatismUtils {

inline bool hasExpired(uint32_t targetMs, uint32_t nowMs) {
  return targetMs != 0 && static_cast<int32_t>(nowMs - targetMs) >= 0;
}

inline bool hasExpired(uint32_t targetMs) {
  return hasExpired(targetMs, millis());
}

inline bool isStillPending(uint32_t targetMs, uint32_t nowMs) {
  return targetMs != 0 && static_cast<int32_t>(targetMs - nowMs) > 0;
}

inline bool isStillPending(uint32_t targetMs) {
  return isStillPending(targetMs, millis());
}

inline uint32_t remainingMs(uint32_t targetMs, uint32_t nowMs) {
  if (targetMs == 0) {
    return 0;
  }
  int32_t diff = static_cast<int32_t>(targetMs - nowMs);
  return diff > 0 ? static_cast<uint32_t>(diff) : 0U;
}

inline uint32_t remainingMs(uint32_t targetMs) {
  return remainingMs(targetMs, millis());
}

template <typename F>
void safeInvoke(F&& fn) {
  if (fn) {
    fn();
  }
}

inline void formatDistanceAlert(char* buffer, size_t bufferSize, const char* prefix, float distance, const char* suffix, float threshold) {
  snprintf(buffer, bufferSize, "%s%.1f cm (%s%.1f)", prefix, distance, suffix, threshold);
}

inline void formatTemperatureAlert(char* buffer, size_t bufferSize, const char* prefix, float temp) {
  snprintf(buffer, bufferSize, "%s%.1f°C", prefix, temp);
}

inline bool parseTruthy(ArduinoJson::JsonVariantConst v) {
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<int>()) return v.as<int>() == 1;
  if (v.is<const char*>()) {
    const char* p = v.as<const char*>();
    if (!p) return false;
    char buffer[32];
    strncpy(buffer, p, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    for (char* c = buffer; *c; c++) {
      if (*c >= 'A' && *c <= 'Z') *c += 32;
    }

    char* start = buffer;
    while (*start == ' ' || *start == '\t') start++;

    char* end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t')) {
      *end = '\0';
      end--;
    }

    return strcmp(start, "1") == 0 || strcmp(start, "true") == 0 ||
           strcmp(start, "on") == 0 || strcmp(start, "checked") == 0 ||
           strcmp(start, "yes") == 0;
  }
  return false;
}

}  // namespace AutomatismUtils

