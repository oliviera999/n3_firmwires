#pragma once
// =============================================================================
// FFP5CS — Décision de durée du remplissage (timing) — logique pure
// =============================================================================
// Extrait de Automatism::handleRefillMaxDurationStop (automatism_refill.cpp), chantier
// C4. Décide, à partir du temps écoulé depuis le démarrage de la pompe :
//   - AnomalyReset : durée invraisemblable (> seuil d'anomalie, ~50 min) -> l'appelant
//     remet le chrono à « maintenant » (capte aussi un _pumpStartMs invalide) ;
//   - Continue     : durée max pas encore atteinte -> ne rien faire ;
//   - ForcedStop   : durée max atteinte -> l'appelant coupe la pompe.
//
// L'anomalie est prioritaire (testée en premier), comme l'ancien code inline.
// Arithmétique non signée : un wrap de millis() reste géré côté appelant (elapsed =
// now - start en unsigned). Aucune dépendance Arduino -> testable nativement (g++).
// Les EFFETS DE BORD (stopTankPump, reset chrono, email, sync, retry/lock) restent
// dans l'appelant : evaluate() ne fait QUE décider.
// =============================================================================

#include <cstdint>

namespace RefillDuration {

enum class Decision : uint8_t {
  Continue,      // max pas atteint
  AnomalyReset,  // durée écoulée invraisemblable -> reset du chrono
  ForcedStop     // durée max atteinte -> arrêt forcé
};

// Seuil d'anomalie historique : 50 min (3 000 000 ms).
constexpr uint32_t DEFAULT_ANOMALY_MS = 3000000UL;

// Parité exacte avec l'ancien bloc inline : anomalie (`elapsed > seuil`) testée d'abord,
// puis « continuer » (`elapsed < max`), sinon arrêt forcé. Comparaisons strictes conservées.
inline Decision evaluate(uint32_t elapsedMs, uint32_t maxMs,
                         uint32_t anomalyThresholdMs = DEFAULT_ANOMALY_MS) {
  if (elapsedMs > anomalyThresholdMs) {
    return Decision::AnomalyReset;
  }
  if (elapsedMs < maxMs) {
    return Decision::Continue;
  }
  return Decision::ForcedStop;
}

}  // namespace RefillDuration
