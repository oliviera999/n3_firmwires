#pragma once
// =============================================================================
// FFP5CS — Durée d'un cycle de distribution de nourriture (logique pure)
// =============================================================================
// Centralise la formule de durée d'un cycle de nourrissage, jusqu'ici dupliquée
// sur 6 sites (manualFeedSmall/Big, nourrissage auto, compte à rebours OLED) :
//   cycle = (durée de distribution) + 50 % de marge (retour servo / sécurité),
//   exprimé en millisecondes — historiquement `(durSec + durSec/2) * 1000`.
//
// Aucune dépendance Arduino → testable nativement (g++). Pas de changement de
// comportement : division entière et promotions conservées à l'identique.
// =============================================================================

#include <cstdint>

namespace FeedingTiming {

// Durée totale (ms) d'un cycle de distribution pour une consigne `durationSec`.
// La marge de 50 % couvre le retour du servo en position repos.
// NB : division entière conservée (ex. 5 s -> 5 + 2 = 7 s -> 7000 ms).
inline uint32_t cycleDurationMs(uint16_t durationSec) {
  return (durationSec + (durationSec / 2U)) * 1000UL;
}

}  // namespace FeedingTiming
