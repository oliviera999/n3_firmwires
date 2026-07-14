#pragma once
// Cadence des vérifications OTA périodiques (cohorte deep-sleep n3pp / msp /
// uploadphotosserver) : compteur de secondes cumulées à travers le deep sleep
// (persisté en RTC_DATA_ATTR côté firmware), saturé au plafond de l'intervalle
// pour ne jamais déborder. Logique pure sans dépendance Arduino, testée en
// natif (test_ota_ui). Mutualisé depuis n3pp/msp/uploadphotosserver main.cpp
// (tranche T4.2 du chantier core shared) — sémantique conservée à l'identique.
#include <stdint.h>

namespace OtaPeriodic {

// Intervalle standard entre deux vérifications OTA (2 h), commun aux firmwares
// de la cohorte deep-sleep.
constexpr uint32_t kDefaultIntervalSeconds = 2UL * 60UL * 60UL;

// Vérification due quand le cumul atteint l'intervalle.
inline bool due(uint32_t elapsedSeconds, uint32_t intervalSeconds) {
  return elapsedSeconds >= intervalSeconds;
}

// Secondes restantes avant la prochaine vérification (0 si déjà due).
inline uint32_t remainingSeconds(uint32_t elapsedSeconds, uint32_t intervalSeconds) {
  return due(elapsedSeconds, intervalSeconds) ? 0 : (intervalSeconds - elapsedSeconds);
}

// Cumul saturant : ajoute deltaSeconds au compteur sans jamais dépasser
// l'intervalle (un long passage hors ligne ne fait pas déborder le compteur).
inline uint32_t accumulate(uint32_t elapsedSeconds, uint32_t intervalSeconds,
                           uint32_t deltaSeconds) {
  if (deltaSeconds == 0) return elapsedSeconds;
  if (elapsedSeconds >= intervalSeconds) return elapsedSeconds;
  const uint32_t remaining = intervalSeconds - elapsedSeconds;
  return elapsedSeconds + ((deltaSeconds >= remaining) ? remaining : deltaSeconds);
}

}  // namespace OtaPeriodic
