#pragma once
// =============================================================================
// FFP5CS — Filtrage pur du capteur ultrason (réservoir/cuve étroite)
// =============================================================================
// Extrait de sensor_ultrasonic.cpp (audit §3.8 : rendre testable la logique
// numérique de filtrage, jusque-là enfouie dans un .cpp couplé I2C/NVS/WDT).
//
// Logique PURE : tri, détection bimodale (deux clusters d'échos), médiane,
// rejet d'outliers, et acceptation/refus d'un saut de mesure. Dépend uniquement
// des seuils SensorConfig::Ultrasonic::Tank (config.h). Comportement identique
// à l'original — réimporté dans sensor_ultrasonic.cpp via using-declarations.
// =============================================================================

#include "config.h"
#include <Arduino.h>  // Serial (log debug bimodal)
#include <cstdint>
#include <cstdlib>    // abs

namespace UltrasonicFilter {

namespace TankCfg = SensorConfig::Ultrasonic::Tank;

inline void sortU16(uint16_t* arr, uint8_t n) {
  for (uint8_t i = 0; i + 1 < n; ++i) {
    for (uint8_t j = i + 1; j < n; ++j) {
      if (arr[i] > arr[j]) {
        uint16_t t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
      }
    }
  }
}

// Cuve étroite : en cas de deux clusters (échos courts vs surface réelle),
// retourne la médiane du cluster le plus haut (distance = moins d'eau = sécurité).
inline uint16_t pickTankDistanceFromBatch(uint16_t* readings, uint8_t count) {
  if (count == 0) return 0;
  if (count == 1) return readings[0];

  sortU16(readings, count);

  uint8_t splitAt = 0;
  uint16_t maxGap = 0;
  for (uint8_t i = 0; i + 1 < count; ++i) {
    const uint16_t gap = readings[i + 1] - readings[i];
    if (gap > maxGap) {
      maxGap = gap;
      splitAt = i + 1;
    }
  }

  uint8_t start = 0;
  uint8_t clusterCount = count;
  if (maxGap >= TankCfg::BIMODAL_GAP_MM) {
    const uint8_t lowCount = splitAt;
    const uint8_t highCount = count - splitAt;
    const uint16_t lowMedian = readings[lowCount / 2];
    const uint16_t highMedian = readings[splitAt + highCount / 2];
    if (highMedian >= lowMedian) {
      start = splitAt;
      clusterCount = highCount;
    } else {
      clusterCount = lowCount;
    }
    Serial.printf("[Ultrasonic] Bimodal gap=%u mm, cluster haut start=%u n=%u\n",
                  maxGap, start, clusterCount);
  }

  uint16_t refined[TankCfg::SAMPLES_COUNT];
  uint8_t refinedCount = 0;
  const uint16_t clusterMedian = readings[start + clusterCount / 2];
  for (uint8_t i = start; i < start + clusterCount; ++i) {
    if (abs((int)readings[i] - (int)clusterMedian) <= (int)TankCfg::OUTLIER_SPREAD_MM) {
      refined[refinedCount++] = readings[i];
    }
  }
  if (refinedCount == 0) {
    return clusterMedian;
  }
  sortU16(refined, refinedCount);
  return refined[refinedCount / 2];
}

inline bool acceptTankJump(uint16_t candidate, uint16_t reference, uint8_t keptCount,
                           uint16_t batchSpread, bool expectDrain) {
  if (reference == 0) return true;

  const int delta = abs((int)candidate - (int)reference);
  const bool drainDirection = candidate > reference;
  const bool tightBatch = batchSpread <= TankCfg::TIGHT_BATCH_SPREAD_MM;
  const bool strongBatch = keptCount >= TankCfg::STRONG_BATCH_MIN_READINGS;

  if (drainDirection) {
    if (delta <= (int)TankCfg::DRAIN_MAX_DELTA_MM) return true;
    if (expectDrain && tightBatch && keptCount >= TankCfg::ADVANCED_MIN_VALID_READINGS) {
      return true;
    }
    if (strongBatch && tightBatch) return true;
    return false;
  }

  // Remplissage ou écho court : plus strict (évite faux « plein »)
  if (delta <= (int)TankCfg::REFILL_MAX_DELTA_MM && strongBatch && tightBatch) {
    return true;
  }
  return false;
}

}  // namespace UltrasonicFilter
