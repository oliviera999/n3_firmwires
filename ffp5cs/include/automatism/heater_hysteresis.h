#pragma once
// =============================================================================
// FFP5CS — Régulation chauffage par hystérésis (logique pure)
// =============================================================================
// Extrait de Automatism::handleAlerts (automatism_display.cpp) dans le cadre du
// découpage de la god-class (chantier C4). Décision pure ON/OFF du chauffage :
//   - ON  si température < seuil            (et chauffage actuellement éteint) ;
//   - OFF si température > seuil + hystérésis (et chauffage actuellement allumé) ;
//   - rien dans la bande morte [seuil, seuil + hystérésis] -> anti-battement.
//
// Sans état interne : l'état « chauffage allumé » (heaterPrevState) reste détenu
// par l'appelant. Aucune dépendance Arduino / config.h -> testable nativement (g++).
// Les EFFETS DE BORD (relais startHeater/stopHeater, email, blink OLED, mise à jour
// du flag heaterPrevState) restent dans l'appelant : evaluate() ne fait QUE décider.
// =============================================================================

namespace HeaterControl {

// Décision retournée à l'appelant (qui détient les effets de bord).
enum class Decision {
  None,     // pas de changement (bande morte ou état déjà conforme)
  TurnOn,   // température sous le seuil -> l'appelant allume le chauffage
  TurnOff   // température au-dessus de seuil+hystérésis -> l'appelant éteint
};

// Décide l'action de chauffage selon la température courante.
//   tempWater     : température mesurée (°C)
//   thresholdC    : seuil de déclenchement du chauffage (°C)
//   hysteresisC   : largeur de bande morte au-dessus du seuil (°C) ; historiquement 2
//   currentlyOn   : état courant du chauffage (heaterPrevState)
//
// Parité exacte avec l'ancien bloc inline : comparaisons strictes (< et >), ON
// évalué avant OFF.
inline Decision evaluate(float tempWater, float thresholdC, float hysteresisC,
                         bool currentlyOn) {
  if (tempWater < thresholdC && !currentlyOn) {
    return Decision::TurnOn;
  }
  if (tempWater > thresholdC + hysteresisC && currentlyOn) {
    return Decision::TurnOff;
  }
  return Decision::None;
}

}  // namespace HeaterControl
