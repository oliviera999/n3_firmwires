#pragma once
// =============================================================================
// n3_time — Validation/comparaison d'epoch (anti-overflow 32-bit)
// =============================================================================
// Mutualisé depuis ffp5cs/include/epoch_util.h (audit §3.8). Logique pure de
// validation d'epoch. POINT CRITIQUE : EPOCH_MAX_VALID (2050) dépasse INT32_MAX,
// donc la comparaison se fait en UNSIGNED pour éviter un overflow sur time_t
// 32-bit. Un bug ici fausse l'acceptation/rejet des horloges NTP/RTC.
//
// Pur (uniquement <ctime>) ; bornes passées en paramètres (pas de dépendance
// à config.h) pour rester testable de façon autonome.
// =============================================================================

#include <ctime>

namespace EpochUtil {

// Valide qu'un epoch est non nul et dans [minValid, maxValid], en comparaison
// non signée (robuste si maxValid > INT32_MAX sur time_t 32-bit).
inline bool isValidEpoch(time_t epoch, time_t minValid, time_t maxValid) {
  unsigned long uepoch = (unsigned long)epoch;
  unsigned long umin = (unsigned long)minValid;
  unsigned long umax = (unsigned long)maxValid;
  return epoch != 0 && uepoch >= umin && uepoch <= umax;
}

// Écart absolu entre deux epochs.
inline time_t epochAbsDiff(time_t a, time_t b) {
  return (a > b) ? (a - b) : (b - a);
}

}  // namespace EpochUtil
