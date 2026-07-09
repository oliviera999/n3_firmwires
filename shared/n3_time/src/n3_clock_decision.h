#pragma once
// =============================================================================
// n3_time — Décisions d'horloge (pures) : plausibilité NTP + dérive RTC
// =============================================================================
// Mutualisé depuis ffp5cs/include/clock_decision.h (chantier C4). Deux logiques
// numériques critiques :
//
//  1. Plausibilité d'une heure NTP fraîchement synchronisée : on n'accepte une
//     sync que si l'année est >= 2024 ET (l'epoch a changé d'au moins N s OU il
//     est proche de la date de compilation). Un faux positif fige une horloge
//     erronée (réveils manqués / horodatage faux) ; un faux négatif gaspille la
//     sync. epochAbsDiff est réutilisé depuis n3_epoch_util.h (déjà testé).
//
//  2. Mesure de la dérive RTC en PPM : ppm = (réel - attendu) / attendu * 1e6,
//     borné à [-max, +max]. Une dérive aberrante non bornée -> correction
//     d'horloge excessive. Tout est en float, sans état ni I/O.
//
// Pur : <cmath>/<ctime> + n3_epoch_util.h uniquement. Aucune dépendance Arduino.h
// ni config.h ; bornes/seuils passés en paramètres (mêmes valeurs par défaut que
// les originaux) pour rester testable de façon autonome. L'I/O (RTC read/write,
// NTP fetch, NVS, Serial, settimeofday) reste côté firmware.
// =============================================================================

#include <cmath>
#include <ctime>

#include "n3_epoch_util.h"  // EpochUtil::epochAbsDiff (extrait, testé nativement)

namespace ClockDecision {

// Seuils par défaut (parité avec l'original).
inline constexpr int     NTP_MIN_PLAUSIBLE_YEAR     = 2024;       // année minimale acceptée
inline constexpr time_t  NTP_MIN_EPOCH_DELTA_SEC    = 60;         // saut minimal vs heure locale
inline constexpr time_t  NTP_COMPILE_TOLERANCE_SEC  = 7 * 86400;  // fenêtre +/- autour du build
inline constexpr float   DRIFT_PPM_MAX              = 500.0f;     // plafond PPM (mesure aberrante)

// L'année (tm_year + 1900, format struct tm) est-elle plausible (>= seuil) ?
inline bool isPlausibleYear(int tmYear, int minYear = NTP_MIN_PLAUSIBLE_YEAR) {
  return (tmYear + 1900) >= minYear;
}

// L'heure NTP a-t-elle suffisamment changé OU est-elle proche de la compilation ?
//   epochChangedEnough = |ntp - localBefore|  >= minDeltaSec
//   nearCompile        = |ntp - compileTime|  <  compileToleranceSec
//   plausible          = epochChangedEnough || nearCompile
inline bool isNtpEpochPlausible(time_t ntpEpoch, time_t localBeforeEpoch,
                                time_t compileTimeEpoch,
                                time_t minDeltaSec = NTP_MIN_EPOCH_DELTA_SEC,
                                time_t compileToleranceSec = NTP_COMPILE_TOLERANCE_SEC) {
  const bool epochChangedEnough =
      EpochUtil::epochAbsDiff(ntpEpoch, localBeforeEpoch) >= minDeltaSec;
  const bool nearCompile =
      EpochUtil::epochAbsDiff(ntpEpoch, compileTimeEpoch) < compileToleranceSec;
  return epochChangedEnough || nearCompile;
}

// Dérive d'horloge en PPM, à partir du temps réel mesuré (timeDiffSec, écart des
// epochs NTP) et du temps attendu (millisDiff converti en secondes).
//   expectedSeconds = millisDiff / 1000          (en float)
//   actualSeconds   = (float)timeDiffSec
//   driftSeconds    = actual - expected
//   si expected > 0 : ppm = driftSeconds / expected * 1e6, borné à [-max, max]
//   sinon           : ppm = 0
// Renvoie 0 si millisDiff == 0 (parité avec le garde-fou amont).
inline float computeDriftPpm(time_t timeDiffSec, unsigned long millisDiff,
                             float ppmMax = DRIFT_PPM_MAX) {
  if (millisDiff == 0) {
    return 0.0f;
  }
  const float expectedSeconds = millisDiff / 1000.0f;
  const float actualSeconds = static_cast<float>(timeDiffSec);
  const float driftSeconds = actualSeconds - expectedSeconds;
  if (expectedSeconds > 0.0f) {
    float ppm = (driftSeconds / expectedSeconds) * 1000000.0f;
    ppm = std::fmax(-ppmMax, std::fmin(ppmMax, ppm));
    return ppm;
  }
  return 0.0f;
}

// Composante "secondes de dérive brutes" (driftSeconds), exposée pour le log
// côté caller. Parité : actual - expected, 0 si millisDiff==0 ou expected <= 0.
inline float computeDriftSeconds(time_t timeDiffSec, unsigned long millisDiff) {
  if (millisDiff == 0) {
    return 0.0f;
  }
  const float expectedSeconds = millisDiff / 1000.0f;
  const float actualSeconds = static_cast<float>(timeDiffSec);
  if (expectedSeconds > 0.0f) {
    return actualSeconds - expectedSeconds;
  }
  return 0.0f;
}

}  // namespace ClockDecision
