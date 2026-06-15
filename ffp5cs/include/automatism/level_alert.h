#pragma once
// =============================================================================
// FFP5CS — Alerte de niveau d'eau par seuil + hystérésis (logique pure)
// =============================================================================
// Extrait de Automatism::handleAlerts (automatism_display.cpp) dans le cadre du
// découpage de la god-class (chantier C4). Décision pure d'alerte « niveau bas »,
// factorisée entre l'aquarium et la réserve (mêmes seuil/hystérésis, seuls les
// effets de bord — message, mail au reset — diffèrent et restent dans l'appelant) :
//   - Raise si distance > seuil d'alerte (et pas déjà en alerte) ;
//   - Clear si déjà en alerte et distance <= seuil de fin (hystérésis) ;
//   - None sinon (bande morte ou état déjà conforme).
//
// Rappel capteur : la distance ultrason est GRANDE quand le niveau d'eau est BAS,
// d'où la comparaison valueMm > alertThrMm pour « niveau bas ».
//
// Sans état interne : le flag « déjà alerté » (_lowAquaSent / _lowTankSent) reste
// détenu par l'appelant. Aucune dépendance Arduino / config.h -> testable en g++.
// Les EFFETS DE BORD (envoi email, blink OLED, mise à jour du flag) restent dans
// l'appelant : evaluate() ne fait QUE décider.
// =============================================================================

#include <cstdint>

namespace LevelAlert {

// Décision retournée à l'appelant (qui détient les effets de bord).
enum class Decision : uint8_t {
  None,   // pas de changement
  Raise,  // niveau bas franchi -> l'appelant lève l'alerte
  Clear   // niveau revenu OK (hystérésis) -> l'appelant clôt l'alerte
};

// Décide l'action d'alerte selon la mesure courante.
//   valueMm        : distance mesurée (mm). GRANDE distance = niveau bas.
//   alertThrMm     : au-dessus -> alerte (cmToMm(seuil))
//   clearThrMm     : <= -> fin d'alerte (cmToMm(seuil - hyst))
//   alreadyAlerted : état courant du flag d'alerte
//
// Parité exacte avec les anciens blocs inline : comparaisons `>` (alerte) et `<=`
// (fin), alerte évaluée avant fin.
inline Decision evaluate(uint16_t valueMm, uint16_t alertThrMm,
                         uint16_t clearThrMm, bool alreadyAlerted) {
  if (valueMm > alertThrMm && !alreadyAlerted) {
    return Decision::Raise;
  }
  if (alreadyAlerted && valueMm <= clearThrMm) {
    return Decision::Clear;
  }
  return Decision::None;
}

}  // namespace LevelAlert
