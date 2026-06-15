#pragma once
// =============================================================================
// FFP5CS — Décision de récupération auto du remplissage (déblocage) — logique pure
// =============================================================================
// Extrait de Automatism::handleRefillAutomaticRecovery (automatism_refill.cpp), chantier
// C4. Quand la pompe est verrouillée pour inefficacité (essais épuisés), on retente
// périodiquement de la débloquer SI la réserve est redevenue franchement OK :
//   recover ssi  verrouillé ET essais épuisés ET (now - dernier essai > debounce)
//                ET distance réserve < seuil OK (avec marge d'hystérésis).
//
// Remplace un `static` local caché (`lastRecoveryAttempt`) par un membre explicite
// `_lastRecoveryAttemptMs` côté Automatism (même correction de smell que
// ReservoirLowSecurity). Les EFFETS DE BORD (déverrouillage, reset essais/flags email,
// mise à jour de l'horodatage, logs) restent dans l'appelant.
//
// Arithmétique non signée (gère le wrap de millis()). Aucune dépendance Arduino -> g++.
// =============================================================================

#include <cstdint>

namespace RefillRecovery {

// Renvoie true si une tentative de récupération (déverrouillage) doit avoir lieu maintenant.
//   locked / retries / maxRetries : état de verrou et compteur d'essais
//   nowMs / lastRecoveryMs / debounceMs : fenêtre anti-rafale entre tentatives
//   wlTankMm < okThresholdMm : réserve franchement revenue OK (seuil - marge d'hystérésis)
// Parité exacte : `> debounce` et `< seuil` stricts, comme l'ancien bloc inline.
inline bool shouldRecover(bool locked, uint8_t retries, uint8_t maxRetries,
                          uint32_t nowMs, uint32_t lastRecoveryMs, uint32_t debounceMs,
                          uint16_t wlTankMm, uint16_t okThresholdMm) {
  if (!(locked && retries >= maxRetries)) {
    return false;
  }
  if (!((nowMs - lastRecoveryMs) > debounceMs)) {
    return false;
  }
  return wlTankMm < okThresholdMm;
}

}  // namespace RefillRecovery
