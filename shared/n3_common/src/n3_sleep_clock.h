#pragma once
// =============================================================================
// n3_common — Correction d'horloge au réveil d'une veille + confiance horloge (logique PURE)
// =============================================================================
// Incident du 06/11/2026 (n3_serveur docs/DIAGNOSTIC_FFP3_PERTE_CONTACT_2026-11-06.md) : à
// chaque réveil le WROOM FFP3 avait une horloge en avance d'UN cycle de veille (2 045 s puis
// 4 090 s) et sa première requête signée était rejetée (fenêtre HMAC 300 s). Cause : le firmware
// AJOUTAIT la durée mesurée de la veille à l'horloge système après `esp_light_sleep_start()`,
// alors qu'ESP-IDF maintient déjà l'horloge système pendant le light sleep (timer RTC) → la
// veille était comptée deux fois.
//
// Règle : ne corriger que ce que l'horloge n'a PAS compté. On compare l'avance de l'horloge
// murale (epoch après − epoch avant) à la durée réellement dormie (compteur monotone) ; la
// correction est la différence, et seulement si elle est significative.
// =============================================================================

#include <stdint.h>

namespace n3clock {

// Au-dessous de cette différence (s), l'horloge est considérée déjà correcte (gigue, arrondi).
constexpr int32_t kSleepCorrectionDeadbandS = 2;

// Secondes à AJOUTER à l'horloge après une veille (négatif si l'horloge a trop avancé — cas
// théorique, on ne recule jamais une horloge ici : retourne 0).
//   wallBefore / wallAfter : epoch avant et après la veille (horloge système)
//   sleptS                : durée réellement dormie (compteur monotone esp_timer)
// Retourne 0 si l'horloge a déjà avancé d'au moins sleptS (comportement ESP-IDF normal).
inline int32_t sleepCorrectionSeconds(uint32_t wallBefore, uint32_t wallAfter, uint32_t sleptS) {
  if (wallAfter < wallBefore) {
    // Horloge reculée pendant la veille (resynchro externe ?) : on n'y touche pas.
    return 0;
  }
  const uint32_t advanced = wallAfter - wallBefore;
  if (advanced + (uint32_t)kSleepCorrectionDeadbandS >= sleptS) {
    return 0;  // déjà compté par l'horloge système (double comptage évité)
  }
  return (int32_t)(sleptS - advanced);
}

// Décision « peut-on signer ? » : l'horloge est fiable si NTP a répondu dans la session OU si une
// heure serveur (X-Server-Ts / Date) a été appliquée il y a moins de maxServerAgeS. Sans l'une ni
// l'autre, on NE signe PAS (le serveur rejetterait et l'incident l'a montré) : la requête part
// sans en-têtes HMAC et le serveur applique sa politique (api_key si non strict).
inline bool clockTrusted(bool ntpSynced, uint32_t serverTimeAgeS, uint32_t maxServerAgeS) {
  if (ntpSynced) return true;
  return serverTimeAgeS != 0xFFFFFFFFUL && serverTimeAgeS <= maxServerAgeS;
}

// Décalage à appliquer depuis une heure serveur : offset = device − serveur (positif = module en
// avance). On n'applique que si |offset| dépasse minApplyS (évite de bouger l'horloge pour la
// latence réseau) ; la latence aller-retour est compensée de moitié.
inline int32_t serverOffsetSeconds(uint32_t deviceEpoch, uint32_t serverEpoch, uint32_t rttMs) {
  const int64_t corrected = (int64_t)serverEpoch + (int64_t)(rttMs / 2000);
  return (int32_t)((int64_t)deviceEpoch - corrected);
}

inline bool shouldApplyServerTime(int32_t offsetS, int32_t minApplyS) {
  return offsetS >= minApplyS || offsetS <= -minApplyS;
}

}  // namespace n3clock
