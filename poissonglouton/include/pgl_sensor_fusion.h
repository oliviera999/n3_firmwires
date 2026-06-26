#pragma once

// -----------------------------------------------------------------------------
// Fusion de capteurs poissonglouton — fonction PURE (testable en natif).
//
// Strategie 3 capteurs {IR, US, PIR} :
//  - Roles : IR et US sont des CAPTEURS DE COMPTAGE (precis, voient le creneau
//    d'une bouteille). PIR est un CAPTEUR DE PRESENCE (quelqu'un est a la
//    machine) ; par defaut il NE compte PAS directement.
//  - Anti-double-comptage : un anti-rebond global unique est applique EN AMONT
//    (dans PglDetection::poll, via lastDetectionMs_ + PGL_DEBOUNCE_MS) avant
//    d'appeler cette fonction. Un meme objet vu par plusieurs capteurs ne
//    produit donc qu'un seul comptage. Ici on ne fait que decider, a partir des
//    triggers DEJA debounce de chaque capteur, s'il faut compter et avec quel
//    niveau de confiance (corroboration).
//  - Robustesse sous-ensemble : le masque presentMask indique quels capteurs
//    sont reellement presents. La logique fonctionne pour 1, 2 ou 3 capteurs et
//    n'est jamais perturbee par un capteur absent (aucun capteur present =>
//    jamais de comptage).
//
// Cette fonction n'inclut QUE <stdint.h> + pgl_types.h (constantes PGL_SENS_*) :
// aucune dependance Arduino, donc compilable et testable cote hote.
// -----------------------------------------------------------------------------

#include <stdint.h>

#include "pgl_types.h"

struct PglFusionInput {
  uint8_t presentMask;             // capteurs presents (PGL_SENS_*)
  bool irTriggered;                // front IR debounce ce poll
  bool usTriggered;                // declenchement US debounce ce poll
  bool pirTriggered;               // front PIR debounce ce poll
  uint32_t lastIrEdgeMs;           // 0 = jamais
  uint32_t lastUsEdgeMs;
  uint32_t lastPirEdgeMs;
  uint32_t nowMs;
  uint32_t corroborationWindowMs;  // fenetre concordance IR<->US
  uint32_t pirPresenceWindowMs;    // fenetre ou un mouvement PIR "confirme" un comptage IR/US
  bool pirGatesCount;              // si true ET PIR present : exige presence PIR pour compter
  bool pirCountsWhenAlone;         // si true : PIR seul (sans IR/US) compte ses fronts (degrade)
};

struct PglFusionResult {
  bool detected;
  uint8_t sensorsMask;
  bool corroborated;
};

PglFusionResult pglFuseDetection(const PglFusionInput& in);
