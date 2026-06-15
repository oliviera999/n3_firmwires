#pragma once
// =============================================================================
// FFP5CS — Décisions de sommeil (pures)
// =============================================================================
// Extrait de automatism_sleep.cpp (audit §3.8). Le délai de sommeil adaptatif
// pilote l'autonomie ET la disponibilité (un mauvais délai = batterie vidée ou
// réveils manqués). Logique pure paramétrée par valeurs (pas le struct
// SleepConfig) -> aucune dépendance config.h/Arduino.
// =============================================================================

#include <algorithm>
#include <cstdint>

namespace SleepDecision {

// Fenêtre de nuit : 19h–6h (économie d'énergie).
inline bool isNightHour(int hour) {
  return (hour >= 19 || hour < 6);
}

// Délai de sommeil adaptatif. Priorité du délai de base : nuit > erreurs.
// Doublé (plafonné à max) en cas d'échecs de réveil consécutifs, puis borné
// à [min, max]. Si l'adaptatif est désactivé, renvoie le délai normal.
inline uint32_t adaptiveSleepDelay(bool adaptiveEnabled,
                                   uint32_t normalSleep, uint32_t errorSleep,
                                   uint32_t nightSleep, uint32_t minSleep,
                                   uint32_t maxSleep,
                                   bool hasRecentErrors, bool isNight,
                                   int consecutiveWakeupFailures) {
  if (!adaptiveEnabled) {
    return normalSleep;
  }
  uint32_t base = normalSleep;
  if (hasRecentErrors) base = errorSleep;
  if (isNight) base = nightSleep;  // la nuit prime sur les erreurs
  if (consecutiveWakeupFailures > 0) {
    base = std::min(base * 2, maxSleep);
  }
  base = std::max(base, minSleep);
  base = std::min(base, maxSleep);
  return base;
}

}  // namespace SleepDecision
