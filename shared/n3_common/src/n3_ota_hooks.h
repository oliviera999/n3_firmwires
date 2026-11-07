#pragma once
// =============================================================================
// n3_common — Points d'accroche (symboles faibles) du cycle OTA
// =============================================================================
// Permettent de journaliser les étapes OTA (check / start / progress / done / fail / paused)
// et de suspendre l'OTA (mode sans échec) SANS que n3_common dépende de n3_data : les
// définitions par défaut (no-op, OTA autorisée) sont dans n3_ota.cpp en `weak` ; la brique
// n3_contact_diag (shared/n3_data) fournit les définitions fortes quand un firmware l'utilise.
// Incident FFP3 06/11/2026 : voir n3_serveur docs/DIAGNOSTIC_FFP3_PERTE_CONTACT_2026-11-06.md.
// =============================================================================

/// Événement OTA (code court + détail borné). Défaut : aucune action.
void n3OtaHookEvent(const char* event, const char* detail);

/// false = l'hôte refuse toute OTA pour ce cycle (mode sans échec). Défaut : true.
bool n3OtaHookUpdatesAllowed();

/// Libellé de carte envoyé dans X-Ota-Board (journal ota_events du serveur). Défaut : nullptr
/// (n3_ota utilise alors metadataKey, ou "n3").
const char* n3OtaHookBoardLabel();
