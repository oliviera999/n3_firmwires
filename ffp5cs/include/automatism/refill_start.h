#pragma once
// =============================================================================
// FFP5CS — Décision de démarrage du remplissage automatique (logique pure)
// =============================================================================
// Extrait de Automatism::handleRefillAutomaticStart (automatism_refill.cpp), chantier
// C4 — découpe de la god-class, étape contrôleur refill (rendue possible par IActuators).
// Décide UNIQUEMENT s'il faut démarrer la pompe de remplissage, la bloquer (réserve
// trop basse), ou ne rien faire. Tous les EFFETS DE BORD (startTankPump, verrouillage,
// email, sendFullUpdate, timers/countdown, logs) restent dans l'appelant.
//
// Rappel capteur : distance ultrason GRANDE = niveau d'eau BAS. Donc :
//   - wlAquaMm > aqThresholdMm  => aquarium sous le niveau cible -> il faut remplir ;
//   - wlTankMm  > tankThresholdMm => réserve trop basse -> blocage (pompe à sec).
//
// Aucune dépendance Arduino/config -> testable nativement (g++).
// =============================================================================

#include <cstdint>

namespace RefillStart {

enum class Decision : uint8_t {
  None,             // pas de démarrage (conditions non réunies / pompe déjà active)
  Start,            // démarrer le remplissage
  BlockReserveLow   // réserve trop basse -> verrouiller la pompe (ne pas démarrer)
};

// Reproduit à l'identique le gating de l'ancien bloc inline (court-circuit conservé
// dans l'ordre des tests ; les comparaisons restent strictes `>` / `<`).
//   wlAquaKnown    : SensorValidation::isWaterLevelKnown(wlAqua)
//   pumpLocked     : tankPumpLocked
//   retries        : tankPumpRetries ; maxRetries : MAX_PUMP_RETRIES
//   manualOverride : _manualTankOverride ; pumpRunning : _acts.isTankPumpRunning()
inline Decision evaluate(bool wlAquaKnown, uint16_t wlAquaMm, uint16_t wlTankMm,
                         uint16_t aqThresholdMm, uint16_t tankThresholdMm,
                         bool pumpLocked, uint8_t retries, uint8_t maxRetries,
                         bool manualOverride, bool pumpRunning) {
  if (!wlAquaKnown) {
    return Decision::None;
  }
  if (wlAquaMm > aqThresholdMm && !pumpLocked && retries < maxRetries && !manualOverride) {
    if (!pumpRunning) {
      if (wlTankMm > tankThresholdMm) {
        return Decision::BlockReserveLow;
      }
      return Decision::Start;
    }
  }
  return Decision::None;
}

}  // namespace RefillStart
