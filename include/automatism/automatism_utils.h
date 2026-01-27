#pragma once

#include <Arduino.h>
#include <cstring>

namespace AutomatismUtils {

// Fonctions de formatage utiles pour éviter la duplication de code
inline void formatDistanceAlert(char* buffer, size_t bufferSize, const char* prefix, float distance, const char* suffix, float threshold) {
  snprintf(buffer, bufferSize, "%s%.1f cm (%s%.1f)", prefix, distance, suffix, threshold);
}

inline void formatTemperatureAlert(char* buffer, size_t bufferSize, const char* prefix, float temp) {
  snprintf(buffer, bufferSize, "%s%.1f°C", prefix, temp);
}

}  // namespace AutomatismUtils

