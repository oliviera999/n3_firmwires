#pragma once

#include <stdint.h>

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

}  // namespace SensorReadingFallback
