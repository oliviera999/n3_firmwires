#pragma once
// =============================================================================
// FFP5CS — Snapshot d'état des actionneurs (sleep/wake) — logique testable
// =============================================================================
// Extrait de Automatism::prepareActuatorsForSleep / restoreActuatorsAfterWake
// (chantier C4, étape suivante de l'inversion `IActuators`). Regroupe les trois
// opérations d'actionneur de la veille/réveil, désormais **testables nativement**
// avec un faux acteur (cf. test/test_actuator_snapshot/) :
//   - capture()  : lit l'état courant (aqua/chauffage/lumière) ;
//   - stopAll()  : coupe ces trois actionneurs (avant veille) ;
//   - apply()    : restaure (rallume) ceux marqués actifs dans le snapshot.
//
// La PERSISTANCE (NVS save/load/clear) et les logs restent dans l'appelant :
// ce module ne touche QUE l'interface IActuators (aucune dépendance Arduino).
// =============================================================================

#include "iactuators.h"

namespace ActuatorSnapshot {

// État capturé des trois actionneurs concernés par la veille.
struct State {
  bool aqua = false;    // pompe aquarium
  bool heater = false;  // chauffage
  bool light = false;   // lumière
};

// Capture l'état courant (lecture seule). Ordre identique à l'ancien code inline.
inline State capture(const IActuators& acts) {
  State s;
  s.aqua = acts.isAquaPumpRunning();
  s.heater = acts.isHeaterOn();
  s.light = acts.isLightOn();
  return s;
}

// Coupe les trois actionneurs (avant veille). Ordre identique : aqua, chauffage, lumière.
inline void stopAll(IActuators& acts) {
  acts.stopAquaPump();
  acts.stopHeater();
  acts.stopLight();
}

// Restaure les actionneurs au réveil : seuls ceux actifs dans le snapshot sont rallumés.
inline void apply(IActuators& acts, const State& snap) {
  if (snap.aqua) acts.startAquaPump();
  if (snap.heater) acts.startHeater();
  if (snap.light) acts.startLight();
}

}  // namespace ActuatorSnapshot
