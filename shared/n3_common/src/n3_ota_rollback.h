#pragma once
// =============================================================================
// Rollback OTA au premier boot (opt-in) — tranche T4.4 du chantier core shared
// =============================================================================
// Capacité identifiée par la veille externe (pattern esp_https_ota / rollback
// natif ESP-IDF, cf. docs/OTA_ROLLBACK_OPT_IN.md) : après un flash OTA, la
// nouvelle image démarre en attente de validation (ESP_OTA_IMG_PENDING_VERIFY) ;
// le firmware exécute un auto-test (WiFi + échange serveur) puis :
//   - auto-test OK  → esp_ota_mark_app_valid_cancel_rollback() (image validée) ;
//   - échec persistant après la fenêtre de grâce → rollback vers l'image
//     précédente (esp_ota_mark_app_invalid_rollback_and_reboot()).
//
// ⚠️ OPT-IN STRICT : tout ce module est inerte sans le flag de build
//   -DN3_OTA_ROLLBACK_ENABLE
// ET le mécanisme n'est effectif que si le bootloader a été construit avec
// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE (pas le cas des cores Arduino
// pré-compilés standards — voir la doc dédiée). Sans cela, l'image n'est
// jamais en PENDING_VERIFY et l'API se réduit à des no-ops sûrs.
// Test sur cible OBLIGATOIRE avant toute généralisation (cahier des charges).
//
// La logique de DÉCISION est pure et testée en natif (test_ota_rollback) ;
// les appels esp_ota_* restent dans n3_ota_rollback.cpp (compilé seulement
// sous flag).
// =============================================================================

#include <stdint.h>

namespace OtaRollback {

// Verdict du cycle de validation du premier boot d'une image OTA.
enum class Action : uint8_t {
  None,       // rien à faire (image déjà validée, ou fenêtre de grâce en cours)
  MarkValid,  // auto-test réussi -> valider l'image (annule le rollback)
  Rollback,   // échec persistant après la fenêtre de grâce -> restaurer l'ancienne image
};

// Décision pure du premier boot :
// - pendingVerify : l'image courante attend validation (ESP_OTA_IMG_PENDING_VERIFY) ;
// - selfTestOk    : l'auto-test applicatif (WiFi + échange serveur) a réussi ;
// - elapsedMs     : temps écoulé depuis le boot ;
// - graceMs       : budget accordé à l'auto-test avant verdict négatif.
// L'auto-test réussi valide immédiatement, même fenêtre expirée (un WiFi lent
// ne doit pas condamner une image saine déjà connectée).
inline Action decide(bool pendingVerify, bool selfTestOk,
                     uint32_t elapsedMs, uint32_t graceMs) {
  if (!pendingVerify) return Action::None;
  if (selfTestOk) return Action::MarkValid;
  if (elapsedMs >= graceMs) return Action::Rollback;
  return Action::None;  // encore dans la fenêtre de grâce, on retentera
}

}  // namespace OtaRollback

#if defined(N3_OTA_ROLLBACK_ENABLE)
// API runtime (ESP32 uniquement, compilée sous flag) — implémentation dans
// n3_ota_rollback.cpp. Toutes défensives : sans support bootloader, l'état
// n'est jamais PENDING_VERIFY et markValid est un no-op qui retourne true.

// true si l'image en cours d'exécution est en attente de validation.
bool n3OtaRollbackIsPendingVerify();

// Valide l'image courante (annule le rollback). true si ESP_OK (ou rien à faire).
bool n3OtaRollbackMarkValid();

// Invalide l'image courante et reboote sur la précédente. Ne retourne pas
// en cas de succès ; retourne false si le rollback est impossible (pas
// d'image de secours valide) — l'appelant décide alors (continuer/reboot).
bool n3OtaRollbackRejectAndReboot();
#endif  // N3_OTA_ROLLBACK_ENABLE
