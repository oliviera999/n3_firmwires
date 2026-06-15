#pragma once
// =============================================================================
// FFP5CS — Décision d'efficacité du remplissage (retry/lock) — logique pure
// =============================================================================
// Extrait de Automatism::handleRefillMaxDurationStop (automatism_refill.cpp), chantier
// C4. Après un arrêt forcé (durée max), évalue si le remplissage a été efficace :
//   - NoEval          : niveau aquarium inconnu -> pas d'évaluation ;
//   - Effective       : amélioration suffisante -> l'appelant remet les essais à 0 ;
//   - Inefficient     : pas d'amélioration, mais essais pas encore épuisés ->
//                       l'appelant incrémente le compteur d'essais ;
//   - InefficientLock : pas d'amélioration ET (essais+1) atteint le max ->
//                       l'appelant incrémente PUIS verrouille la pompe.
//
// Les EFFETS DE BORD (incrément/reset du compteur, verrouillage, email, sync, logs)
// restent dans l'appelant. Aucune dépendance Arduino -> testable nativement (g++).
// =============================================================================

#include <cstdint>

namespace RefillEfficiency {

enum class Decision : uint8_t { NoEval, Effective, Inefficient, InefficientLock };

// levelImprovement : (niveau au démarrage − niveau actuel) en mm ; `< 1` = inefficace
//                    (parité stricte avec l'ancien code, valeurs négatives incluses).
// retriesBefore    : tankPumpRetries AVANT incrément ; maxRetries : MAX_PUMP_RETRIES.
// Le verrou se déclenche quand l'incrément à venir atteint le plafond (`retries+1 >= max`),
// exactement comme l'ancien `++tankPumpRetries; if (tankPumpRetries >= MAX) { lock }`.
inline Decision evaluate(bool levelKnown, int levelImprovement,
                         uint8_t retriesBefore, uint8_t maxRetries) {
  if (!levelKnown) {
    return Decision::NoEval;
  }
  if (levelImprovement < 1) {
    return (static_cast<int>(retriesBefore) + 1 >= static_cast<int>(maxRetries))
               ? Decision::InefficientLock
               : Decision::Inefficient;
  }
  return Decision::Effective;
}

}  // namespace RefillEfficiency
