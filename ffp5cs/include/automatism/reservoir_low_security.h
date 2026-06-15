#pragma once
// =============================================================================
// FFP5CS — Sécurité « réserve (tank) basse » : debounce + hystérésis (logique pure)
// =============================================================================
// Extrait de Automatism::handleRefillReservoirLowSecurity (automatism_refill.cpp)
// dans le cadre du découpage de la god-class (chantier C4). Machine d'état pure :
//   - debounce : N mesures consécutives « réserve basse » avant de verrouiller,
//     M mesures consécutives « réserve OK » avant de déverrouiller ;
//   - hystérésis : le seuil de déverrouillage est plus bas que celui de verrouillage
//     (l'appelant passe les deux seuils déjà convertis en mm) ;
//   - zone morte : entre les deux seuils, les compteurs sont bornés (pas de bascule).
//
// Remplace deux `static` locaux cachés dans la méthode (état global masqué = smell
// god-class) par un `State` explicite détenu par Automatism.
//
// Aucune dépendance Arduino / config.h : paramétrée par valeurs déjà converties
// (distances en mm). Testable nativement (g++).
// Les EFFETS DE BORD (arrêt pompe, email, flags _emailTank*, logs série, set du
// motif de verrou) restent dans l'appelant : evaluate() ne fait QUE décider.
//
// Rappel capteur : la distance ultrason est GRANDE quand la réserve est basse,
// d'où la comparaison wlTankMm > lowThresholdMm pour « réserve basse ».
// =============================================================================

#include <cstdint>

namespace ReservoirLowSecurity {

// État de debounce (détenu par Automatism). Remplace deux static locaux.
struct State {
  uint8_t aboveCount = 0;  // mesures consécutives « réserve basse » (distance > seuil)
  uint8_t belowCount = 0;  // mesures consécutives « réserve OK »   (distance < seuil-hyst)
};

// Décision retournée à l'appelant (qui détient les effets de bord).
enum class Decision : uint8_t {
  None,    // pas de bascule (debounce non atteint ou zone morte)
  Lock,    // réserve basse confirmée -> l'appelant verrouille la pompe
  Unlock   // réserve revenue OK et confirmée -> l'appelant déverrouille
};

// Met à jour les compteurs de `st` selon la mesure courante et retourne la décision.
//   wlTankMm        : distance mesurée réserve (mm). GRANDE distance = réserve basse.
//   lowThresholdMm  : au-dessus = « basse » (cmToMm(seuil))
//   okThresholdMm   : en-dessous = « OK »   (cmToMm(seuil - hyst))
//   currentlyLocked : état de verrou courant de la pompe
//
// Parité exacte avec l'ancien bloc inline : incrément borné à 3, bascule à 2 (lock)
// / 3 (unlock), bornage à 2 dans la zone morte.
inline Decision evaluate(State& st, uint16_t wlTankMm,
                         uint16_t lowThresholdMm, uint16_t okThresholdMm,
                         bool currentlyLocked) {
  if (wlTankMm > lowThresholdMm) {
    st.aboveCount = (uint8_t)(st.aboveCount < 3 ? st.aboveCount + 1 : 3);
    st.belowCount = 0;
    if (!currentlyLocked && st.aboveCount >= 2) {
      return Decision::Lock;
    }
  } else if (wlTankMm < okThresholdMm) {
    st.belowCount = (uint8_t)(st.belowCount < 3 ? st.belowCount + 1 : 3);
    st.aboveCount = 0;
    if (currentlyLocked && st.belowCount >= 3) {
      return Decision::Unlock;
    }
  } else {
    if (st.aboveCount > 2) st.aboveCount = 2;
    if (st.belowCount > 2) st.belowCount = 2;
  }
  return Decision::None;
}

}  // namespace ReservoirLowSecurity
