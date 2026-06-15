#pragma once
// =============================================================================
// FFP5CS — Soustraction saturée pour seuils (garde anti-underflow) — logique pure
// =============================================================================
// Centralise le motif `(seuil > marge) ? (seuil - marge) : 0`, dupliqué dans les
// calculs de seuil d'hystérésis (alertes aquarium/réserve, sécurité réserve basse,
// récupération pompe). Sur type non signé, `seuil - marge` déborde si marge > seuil ;
// ce helper garantit un plancher à 0.
//
// Aucune dépendance Arduino → testable nativement (g++).
// =============================================================================

#include <cstdint>

namespace ThresholdUtil {

// Soustraction saturée : value - sub, plancher à 0 (pas d'underflow non signé).
inline uint16_t subSat(uint16_t value, uint16_t sub) {
  return (value > sub) ? static_cast<uint16_t>(value - sub) : static_cast<uint16_t>(0);
}

}  // namespace ThresholdUtil
