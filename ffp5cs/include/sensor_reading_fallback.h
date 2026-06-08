#pragma once

#include <stdint.h>
#include <stdio.h>

namespace SensorReadingFallback {

inline uint16_t waterLevel(uint16_t current, uint16_t lastValid, uint16_t fallback) {
  if (current > 0) {
    return current;
  }
  if (lastValid > 0) {
    return lastValid;
  }
  return fallback;
}

/** useFallback=false : mesure absente (0) si current invalide, sans lastValid ni défaut. */
inline uint16_t resolveWaterLevel(uint16_t current, uint16_t lastValid,
                                  uint16_t fallback, bool useFallback) {
  if (current > 0) {
    return current;
  }
  if (!useFallback) {
    return 0;
  }
  return waterLevel(current, lastValid, fallback);
}

inline void formatWaterLevelPost(char* buf, size_t bufSize, uint16_t wl) {
  if (!buf || bufSize == 0) {
    return;
  }
  if (wl == 0) {
    buf[0] = '\0';
  } else {
    snprintf(buf, bufSize, "%u", static_cast<unsigned>(wl));
  }
}

}  // namespace SensorReadingFallback
