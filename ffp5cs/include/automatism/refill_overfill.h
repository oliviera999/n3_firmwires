#pragma once
// =============================================================================
// FFP5CS — Décision de sécurité « aquarium trop plein » (logique pure)
// =============================================================================
// Extrait de Automatism::handleRefillAquariumOverfillSecurity (automatism_refill.cpp),
// chantier C4. Sécurité : si l'aquarium est trop plein (distance ultrason FAIBLE, donc
// niveau d'eau HAUT < seuil limFlood), on interdit le remplissage (verrou pompe). On
// déverrouille quand le niveau redescend, sauf si une alerte « flood » est encore active.
//   - Lock   : trop plein détecté et pas déjà verrouillé pour cette raison ;
//   - Unlock : niveau revenu OK, verrou « trop plein » actif et plus en flood ;
//   - None   : rien à faire (niveau inconnu, état déjà conforme, ou flood encore actif).
//
// L'appelant détient les effets de bord (set/clear du verrou + motif, arrêt pompe,
// notification, reset des flags email, logs). Aucune dépendance Arduino -> testable g++.
//
// Rappel capteur : distance FAIBLE = niveau HAUT. D'où `wlAquaMm < limFloodMm` = trop plein.
// =============================================================================

#include <cstdint>

namespace RefillOverfill {

enum class Decision : uint8_t {
  None,    // pas de bascule
  Lock,    // trop plein -> verrouiller la pompe (motif AQUARIUM_OVERFILL)
  Unlock   // niveau revenu OK -> déverrouiller
};

// wlAquaKnown          : SensorValidation::isWaterLevelKnown(wlAqua)
// limFloodMm           : cmToMm(getLimFlood()) ; sous cette distance = trop plein
// lockedForOverfill    : tankPumpLocked && _tankPumpLockReason == AQUARIUM_OVERFILL
// inFlood              : alerte trop-plein en cours (empêche le déverrouillage)
//
// Parité exacte : Lock seulement à la transition (pas déjà verrouillé pour ce motif) ;
// Unlock seulement si verrouillé pour ce motif et plus en flood.
inline Decision evaluate(bool wlAquaKnown, uint16_t wlAquaMm, uint16_t limFloodMm,
                         bool lockedForOverfill, bool inFlood) {
  if (!wlAquaKnown) {
    return Decision::None;
  }
  if (wlAquaMm < limFloodMm) {
    return lockedForOverfill ? Decision::None : Decision::Lock;
  }
  return (lockedForOverfill && !inFlood) ? Decision::Unlock : Decision::None;
}

}  // namespace RefillOverfill
