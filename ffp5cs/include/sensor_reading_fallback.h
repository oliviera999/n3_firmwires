#pragma once
// =============================================================================
// FFP5CS — Cascade de repli des lectures : wrapper de compat vers shared/
// =============================================================================
// v15.16 (mutualisation T2) : la logique vit dans shared/n3_analog_sensors
// (n3_sensor_fallback.h, API renommée NEUTRE : resolve / resolveOrZero /
// formatPostValue). Ce wrapper conserve l'ancien vocabulaire « waterLevel »
// pour les consommateurs ffp5cs (system_sensors, automatism_sync, tests).
// Forwards inline purs — zéro coût, logique identique.
// =============================================================================

#include "n3_sensor_fallback.h"

namespace SensorReadingFallback {

inline uint16_t waterLevel(uint16_t current, uint16_t lastValid, uint16_t fallback) {
  return N3SensorFallback::resolve(current, lastValid, fallback);
}

/** useFallback=false : mesure absente (0) si current invalide, sans lastValid ni défaut. */
inline uint16_t resolveWaterLevel(uint16_t current, uint16_t lastValid,
                                  uint16_t fallback, bool useFallback) {
  return N3SensorFallback::resolveOrZero(current, lastValid, fallback, useFallback);
}

inline void formatWaterLevelPost(char* buf, size_t bufSize, uint16_t wl) {
  N3SensorFallback::formatPostValue(buf, bufSize, wl);
}

}  // namespace SensorReadingFallback
