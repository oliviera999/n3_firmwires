// Rollback OTA au premier boot (opt-in) — voir n3_ota_rollback.h et
// docs/OTA_ROLLBACK_OPT_IN.md. Translation unit vide sans le flag de build
// N3_OTA_ROLLBACK_ENABLE : aucun coût flash/RAM pour les firmwares non opt-in.
//
// Inspiré du mécanisme de rollback natif ESP-IDF (app_update / esp_ota_ops,
// https://github.com/espressif/esp-idf — API esp_ota_mark_app_valid_cancel_rollback
// / esp_ota_mark_app_invalid_rollback_and_reboot, licence Apache-2.0), adapté
// aux conventions du dépôt (logs série préfixés, API défensive sans exception).
#if defined(N3_OTA_ROLLBACK_ENABLE)

#include "n3_ota_rollback.h"

#include <Arduino.h>
#include <esp_ota_ops.h>

bool n3OtaRollbackIsPendingVerify() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) return false;

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return false;
  }
  // Sans CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, l'etat reste UNDEFINED :
  // le mecanisme est alors inerte (aucune image n'attend de validation).
  return state == ESP_OTA_IMG_PENDING_VERIFY;
}

bool n3OtaRollbackMarkValid() {
  if (!n3OtaRollbackIsPendingVerify()) {
    return true;  // rien a valider : no-op sur
  }
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) {
    Serial.printf("[OTA][ROLLBACK] validation image impossible: err=0x%x\n", err);
    return false;
  }
  Serial.println("[OTA][ROLLBACK] image validee (rollback annule)");
  return true;
}

bool n3OtaRollbackRejectAndReboot() {
  Serial.println("[OTA][ROLLBACK] auto-test en echec: retour a l'image precedente...");
  Serial.flush();
  const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
  // On n'arrive ici que si le rollback a echoue (pas d'image de secours valide).
  Serial.printf("[OTA][ROLLBACK] rollback impossible: err=0x%x (pas d'image de secours ?)\n", err);
  return false;
}

#endif  // N3_OTA_ROLLBACK_ENABLE
